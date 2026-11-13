/*
  VitaShell for Switch 2
  Copyright (C) 2026, VitaShell Switch 2 port contributors

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
 * sceKernel threads, lightweight mutexes, delays and clocks over newlib's
 * pthreads, which libounce-nx implements with Horizon threads.
 *
 * Vita semantics kept here:
 *  - sceKernelStartThread copies the argument block, so callers may pass a
 *    pointer to a local structure (VitaShell does this for every worker).
 *  - A thread may end with sceKernelExitDeleteThread; another thread may
 *    still wait for it with sceKernelWaitThreadEnd.
 * Threads nobody waits for are joined when their slot is reused.
 */

#include <vitasdk.h>

#include <switch.h>

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_THREADS 64
#define MIN_STACK_SIZE (256 * 1024)

/* Thread UIDs: (generation << 8) | slot, always positive. */
#define THREAD_SLOT_BITS 8
#define THREAD_SLOT_MASK ((1 << THREAD_SLOT_BITS) - 1)

enum ThreadSlotState {
  THREAD_SLOT_FREE,
  THREAD_SLOT_CREATED,
  THREAD_SLOT_RUNNING,
  THREAD_SLOT_FINISHED,
};

typedef struct {
  int state;
  int generation;
  pthread_t thread;
  SceKernelThreadEntry entry;
  size_t stack_size;
  SceSize args_size;
  void *args;
  int exit_status;
  int waited; // a sceKernelWaitThreadEnd caller joins it
} ThreadSlot;

static ThreadSlot thread_slots[MAX_THREADS];
static pthread_mutex_t thread_slots_lock = PTHREAD_MUTEX_INITIALIZER;

static int slotUid(int index) {
  return ((thread_slots[index].generation & 0x3FFFFF) << THREAD_SLOT_BITS) | index;
}

/* Returns the slot of a UID, or NULL. Called with the lock held. */
static ThreadSlot *findSlot(SceUID thid) {
  if (thid <= 0)
    return NULL;

  int index = thid & THREAD_SLOT_MASK;
  if (index >= MAX_THREADS)
    return NULL;

  ThreadSlot *slot = &thread_slots[index];
  if (slot->state == THREAD_SLOT_FREE || slotUid(index) != thid)
    return NULL;

  return slot;
}

/* Frees a slot whose thread has been joined (or never started). */
static void releaseSlot(ThreadSlot *slot) {
  free(slot->args);
  slot->args = NULL;
  slot->state = THREAD_SLOT_FREE;
}

static ThreadSlot *findCurrentSlot(void) {
  pthread_t self = pthread_self();
  int i;
  for (i = 0; i < MAX_THREADS; i++) {
    if (thread_slots[i].state == THREAD_SLOT_RUNNING &&
        pthread_equal(thread_slots[i].thread, self))
      return &thread_slots[i];
  }
  return NULL;
}

static void markCurrentFinished(int status) {
  pthread_mutex_lock(&thread_slots_lock);
  ThreadSlot *slot = findCurrentSlot();
  if (slot) {
    slot->exit_status = status;
    slot->state = THREAD_SLOT_FINISHED;
  }
  pthread_mutex_unlock(&thread_slots_lock);
}

static void *threadTrampoline(void *argument) {
  ThreadSlot *slot = argument;
  int status = slot->entry(slot->args_size, slot->args);
  markCurrentFinished(status);
  return NULL;
}

/* Joins finished threads that nobody waited for, so their slots can be
   reused. Called with the lock held; finished threads exit promptly. */
static void reapFinishedThreads(void) {
  int i;
  for (i = 0; i < MAX_THREADS; i++) {
    if (thread_slots[i].state == THREAD_SLOT_FINISHED && !thread_slots[i].waited) {
      pthread_join(thread_slots[i].thread, NULL);
      releaseSlot(&thread_slots[i]);
    }
  }
}

SceUID sceKernelCreateThread(const char *name, SceKernelThreadEntry entry, int priority,
                             int stack_size, SceUInt32 attributes, int cpu_affinity,
                             const void *options) {
  (void)name;
  (void)priority;
  (void)attributes;
  (void)cpu_affinity;
  (void)options;

  if (!entry)
    return SCE_ERROR_FROM_ERRNO(EINVAL);

  pthread_mutex_lock(&thread_slots_lock);

  int index;
  for (index = 0; index < MAX_THREADS; index++) {
    if (thread_slots[index].state == THREAD_SLOT_FREE)
      break;
  }

  if (index == MAX_THREADS) {
    reapFinishedThreads();
    for (index = 0; index < MAX_THREADS; index++) {
      if (thread_slots[index].state == THREAD_SLOT_FREE)
        break;
    }
  }

  if (index == MAX_THREADS) {
    pthread_mutex_unlock(&thread_slots_lock);
    return SCE_ERROR_FROM_ERRNO(EAGAIN);
  }

  ThreadSlot *slot = &thread_slots[index];
  slot->state = THREAD_SLOT_CREATED;
  slot->generation++;
  slot->entry = entry;
  slot->stack_size = stack_size > MIN_STACK_SIZE ? (size_t)stack_size : MIN_STACK_SIZE;
  slot->args_size = 0;
  slot->args = NULL;
  slot->exit_status = 0;
  slot->waited = 0;

  SceUID thid = slotUid(index);
  pthread_mutex_unlock(&thread_slots_lock);
  return thid;
}

int sceKernelStartThread(SceUID thid, SceSize args_size, void *argp) {
  pthread_mutex_lock(&thread_slots_lock);

  ThreadSlot *slot = findSlot(thid);
  if (!slot || slot->state != THREAD_SLOT_CREATED) {
    pthread_mutex_unlock(&thread_slots_lock);
    return SCE_ERROR_FROM_ERRNO(ESRCH);
  }

  // The Vita copies the arguments onto the new thread's stack.
  if (args_size > 0 && argp) {
    slot->args = malloc(args_size);
    if (!slot->args) {
      pthread_mutex_unlock(&thread_slots_lock);
      return SCE_ERROR_FROM_ERRNO(ENOMEM);
    }
    memcpy(slot->args, argp, args_size);
    slot->args_size = args_size;
  }

  pthread_attr_t attributes;
  pthread_attr_init(&attributes);
  pthread_attr_setstacksize(&attributes, slot->stack_size);

  slot->state = THREAD_SLOT_RUNNING;
  int result = pthread_create(&slot->thread, &attributes, threadTrampoline, slot);
  pthread_attr_destroy(&attributes);

  if (result != 0) {
    slot->state = THREAD_SLOT_CREATED;
    free(slot->args);
    slot->args = NULL;
    pthread_mutex_unlock(&thread_slots_lock);
    return SCE_ERROR_FROM_ERRNO(result);
  }

  pthread_mutex_unlock(&thread_slots_lock);
  return 0;
}

int sceKernelWaitThreadEnd(SceUID thid, int *status, SceUInt32 *timeout) {
  (void)timeout;

  pthread_mutex_lock(&thread_slots_lock);
  ThreadSlot *slot = findSlot(thid);
  if (!slot) {
    // Already reaped: it has ended.
    pthread_mutex_unlock(&thread_slots_lock);
    return 0;
  }

  if (slot->state == THREAD_SLOT_CREATED || slot->waited) {
    pthread_mutex_unlock(&thread_slots_lock);
    return SCE_ERROR_FROM_ERRNO(ESRCH);
  }

  // Claim the join so that the reaper leaves this thread alone.
  pthread_t thread = slot->thread;
  slot->waited = 1;
  pthread_mutex_unlock(&thread_slots_lock);

  pthread_join(thread, NULL);

  pthread_mutex_lock(&thread_slots_lock);
  slot = findSlot(thid);
  if (slot) {
    if (status)
      *status = slot->exit_status;
    releaseSlot(slot);
  }
  pthread_mutex_unlock(&thread_slots_lock);

  return 0;
}

int sceKernelDeleteThread(SceUID thid) {
  pthread_mutex_lock(&thread_slots_lock);
  ThreadSlot *slot = findSlot(thid);
  if (slot && slot->state == THREAD_SLOT_CREATED)
    releaseSlot(slot);
  pthread_mutex_unlock(&thread_slots_lock);
  return 0;
}

int sceKernelExitThread(int status) {
  markCurrentFinished(status);
  pthread_exit(NULL);
  return 0;
}

int sceKernelExitDeleteThread(int status) {
  return sceKernelExitThread(status);
}

int sceKernelDelayThread(SceUInt32 microseconds) {
  svcSleepThread((s64)microseconds * 1000);
  return 0;
}

int sceKernelCreateLwMutex(SceKernelLwMutexWork *work, const char *name, unsigned int attributes,
                           int initial_count, const void *options) {
  (void)name;
  (void)attributes;
  (void)initial_count;
  (void)options;

  pthread_mutexattr_t mutex_attributes;
  pthread_mutexattr_init(&mutex_attributes);
  pthread_mutexattr_settype(&mutex_attributes, PTHREAD_MUTEX_RECURSIVE);
  int result = pthread_mutex_init(&work->mutex, &mutex_attributes);
  pthread_mutexattr_destroy(&mutex_attributes);

  return result ? SCE_ERROR_FROM_ERRNO(result) : 0;
}

int sceKernelLockLwMutex(SceKernelLwMutexWork *work, int lock_count, unsigned int *timeout) {
  (void)lock_count;
  (void)timeout;
  pthread_mutex_lock(&work->mutex);
  return 0;
}

int sceKernelUnlockLwMutex(SceKernelLwMutexWork *work, int unlock_count) {
  (void)unlock_count;
  pthread_mutex_unlock(&work->mutex);
  return 0;
}

/* Kernel mutexes: a table of pthread mutexes. UIDs are (slot + 1) with a
   marker bit, so they are positive and never zero. */

#define MAX_MUTEXES 32
#define MUTEX_UID_MARKER 0x00100000

typedef struct {
  int used;
  pthread_mutex_t mutex;
} MutexSlot;

static MutexSlot mutex_slots[MAX_MUTEXES];
static pthread_mutex_t mutex_slots_lock = PTHREAD_MUTEX_INITIALIZER;

static MutexSlot *findMutex(SceUID mutex_id) {
  if ((mutex_id & MUTEX_UID_MARKER) == 0)
    return NULL;

  int index = (mutex_id & ~MUTEX_UID_MARKER) - 1;
  if (index < 0 || index >= MAX_MUTEXES || !mutex_slots[index].used)
    return NULL;

  return &mutex_slots[index];
}

SceUID sceKernelCreateMutex(const char *name, SceUInt32 attributes, int initial_count,
                            const void *options) {
  (void)name;
  (void)attributes;
  (void)options;

  pthread_mutex_lock(&mutex_slots_lock);

  int index;
  for (index = 0; index < MAX_MUTEXES; index++) {
    if (!mutex_slots[index].used)
      break;
  }

  if (index == MAX_MUTEXES) {
    pthread_mutex_unlock(&mutex_slots_lock);
    return SCE_ERROR_FROM_ERRNO(ENOMEM);
  }

  int result = pthread_mutex_init(&mutex_slots[index].mutex, NULL);
  if (result == 0) {
    mutex_slots[index].used = 1;
    if (initial_count > 0)
      pthread_mutex_lock(&mutex_slots[index].mutex);
  }

  pthread_mutex_unlock(&mutex_slots_lock);

  if (result != 0)
    return SCE_ERROR_FROM_ERRNO(result);

  return MUTEX_UID_MARKER | (index + 1);
}

int sceKernelDeleteMutex(SceUID mutex_id) {
  pthread_mutex_lock(&mutex_slots_lock);

  MutexSlot *slot = findMutex(mutex_id);
  if (slot) {
    pthread_mutex_destroy(&slot->mutex);
    slot->used = 0;
  }

  pthread_mutex_unlock(&mutex_slots_lock);
  return slot ? 0 : SCE_ERROR_FROM_ERRNO(EINVAL);
}

int sceKernelLockMutex(SceUID mutex_id, int lock_count, unsigned int *timeout) {
  (void)lock_count;
  (void)timeout;

  MutexSlot *slot = findMutex(mutex_id);
  if (!slot)
    return SCE_ERROR_FROM_ERRNO(EINVAL);

  pthread_mutex_lock(&slot->mutex);
  return 0;
}

int sceKernelUnlockMutex(SceUID mutex_id, int unlock_count) {
  (void)unlock_count;

  MutexSlot *slot = findMutex(mutex_id);
  if (!slot)
    return SCE_ERROR_FROM_ERRNO(EINVAL);

  pthread_mutex_unlock(&slot->mutex);
  return 0;
}

SceUInt64 sceKernelGetProcessTimeWide(void) {
  // The system tick does not depend on the time service.
  return armTicksToNs(armGetSystemTick()) / 1000ULL;
}

SceUInt64 sceKernelGetSystemTimeWide(void) {
  return sceKernelGetProcessTimeWide();
}
