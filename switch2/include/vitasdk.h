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
 * The subset of the PS Vita SDK interface that VitaShell's portable code
 * uses, reimplemented for Switch 2 on libounce-nx, newlib and SDL 2.
 * Written from VitaShell's call sites and the public vitasdk documentation;
 * no Sony SDK code is used. The implementations are in switch2/src/psp2_*.c.
 * Types and constants keep vitasdk's names and values so that VitaShell's
 * own code (for example its message dialog result codes) stays unchanged.
 */

#ifndef VITASHELL_SWITCH2_VITASDK_H
#define VITASHELL_SWITCH2_VITASDK_H

#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <time.h>
#include <sys/stat.h>

/* newlib defines st_atime/st_mtime/st_ctime as macros for its timespec
   members; SceIoStat has members with those names. Code that needs the
   POSIX times uses st_atim/st_mtim/st_ctim. */
#undef st_atime
#undef st_mtime
#undef st_ctime

#ifdef __cplusplus
extern "C" {
#endif

/* Scalar types */

typedef int32_t SceUID;
typedef int64_t SceOff;
typedef uint32_t SceSize;
typedef int32_t SceMode;
typedef int32_t SceInt32;
typedef uint32_t SceUInt32;
typedef int64_t SceInt64;
typedef uint64_t SceUInt64;
typedef uint8_t SceChar8;
typedef int SceBool;

#define SCE_TRUE 1
#define SCE_FALSE 0

/* Error codes: SCE_ERROR_ERRNO_* is 0x80010000 | errno, as on the Vita. */

#define SCE_ERROR_ERRNO_BASE 0x80010000
#define SCE_ERROR_FROM_ERRNO(e) ((int)(SCE_ERROR_ERRNO_BASE | ((e) & 0xFFFF)))

/* Date and time (sceRtc). Times in SceIoStat are UTC, as on the Vita. */

typedef struct SceDateTime {
  unsigned short year;
  unsigned short month;
  unsigned short day;
  unsigned short hour;
  unsigned short minute;
  unsigned short second;
  unsigned int microsecond;
} SceDateTime;

/* Microseconds since 0001-01-01 00:00:00. */
typedef struct SceRtcTick {
  SceUInt64 tick;
} SceRtcTick;

int sceRtcGetCurrentClock(SceDateTime *time, int time_zone);
int sceRtcGetCurrentClockLocalTime(SceDateTime *time);
int sceRtcGetTick(const SceDateTime *time, SceRtcTick *tick);
int sceRtcSetTick(SceDateTime *time, const SceRtcTick *tick);
int sceRtcSetTime_t(SceDateTime *time, time_t value);
int sceRtcConvertUtcToLocalTime(const SceRtcTick *utc, SceRtcTick *local_time);
int sceRtcConvertLocalTimeToUtc(const SceRtcTick *local_time, SceRtcTick *utc);

/* File I/O (sceIo) */

#define SCE_O_RDONLY 0x0001
#define SCE_O_WRONLY 0x0002
#define SCE_O_RDWR   (SCE_O_RDONLY | SCE_O_WRONLY)
#define SCE_O_NBLOCK 0x0004
#define SCE_O_APPEND 0x0100
#define SCE_O_CREAT  0x0200
#define SCE_O_TRUNC  0x0400
#define SCE_O_EXCL   0x0800

#define SCE_SEEK_SET 0
#define SCE_SEEK_CUR 1
#define SCE_SEEK_END 2

#define SCE_S_IXOTH 0x0001
#define SCE_S_IWOTH 0x0002
#define SCE_S_IROTH 0x0004
#define SCE_S_IXGRP 0x0008
#define SCE_S_IWGRP 0x0010
#define SCE_S_IRGRP 0x0020
#define SCE_S_IXUSR 0x0040
#define SCE_S_IWUSR 0x0080
#define SCE_S_IRUSR 0x0100
#define SCE_S_IFDIR 0x1000
#define SCE_S_IFREG 0x2000
#define SCE_S_IFLNK 0x4000
#define SCE_S_IFMT  0xF000

#define SCE_S_ISDIR(m) (((m) & SCE_S_IFMT) == SCE_S_IFDIR)
#define SCE_S_ISREG(m) (((m) & SCE_S_IFMT) == SCE_S_IFREG)

typedef struct SceIoStat {
  SceMode st_mode;
  unsigned int st_attr;
  SceOff st_size;
  SceDateTime st_ctime;
  SceDateTime st_atime;
  SceDateTime st_mtime;
  unsigned int st_private[6];
} SceIoStat;

typedef struct SceIoDirent {
  SceIoStat d_stat;
  char d_name[256];
  void *d_private;
  int dummy;
} SceIoDirent;

/* The reply of sceIoDevctl(device, 0x3001, ...): device capacity. */
typedef struct SceIoDevInfo {
  SceOff max_size;
  SceOff free_size;
  SceSize cluster_size;
  void *unk;
} SceIoDevInfo;

#define SCE_IO_DEVCTL_GET_DEVICE_INFO 0x3001

SceUID sceIoOpen(const char *path, int flags, SceMode mode);
int sceIoClose(SceUID fd);
int sceIoRead(SceUID fd, void *data, SceSize size);
int sceIoWrite(SceUID fd, const void *data, SceSize size);
SceOff sceIoLseek(SceUID fd, SceOff offset, int whence);
int sceIoLseek32(SceUID fd, int offset, int whence);
int sceIoSyncByFd(SceUID fd, int flags);
int sceIoRemove(const char *path);
int sceIoRename(const char *old_path, const char *new_path);
int sceIoMkdir(const char *path, SceMode mode);
int sceIoRmdir(const char *path);
SceUID sceIoDopen(const char *path);
int sceIoDread(SceUID fd, SceIoDirent *entry);
int sceIoDclose(SceUID fd);
int sceIoGetstat(const char *path, SceIoStat *stat);
int sceIoGetstatByFd(SceUID fd, SceIoStat *stat);
int sceIoChstat(const char *path, SceIoStat *stat, int bits);
int sceIoChstatByFd(SceUID fd, const SceIoStat *stat, unsigned int bits);
int sceIoDevctl(const char *device, unsigned int command, void *input, int input_size,
                void *output, int output_size);

/* Threads and synchronization (sceKernel) */

typedef int (*SceKernelThreadEntry)(SceSize args, void *argp);

typedef struct SceKernelLwMutexWork {
  pthread_mutex_t mutex;
} SceKernelLwMutexWork;

SceUID sceKernelCreateThread(const char *name, SceKernelThreadEntry entry, int priority,
                             int stack_size, SceUInt32 attributes, int cpu_affinity,
                             const void *options);
int sceKernelStartThread(SceUID thid, SceSize args_size, void *argp);
int sceKernelWaitThreadEnd(SceUID thid, int *status, SceUInt32 *timeout);
int sceKernelDeleteThread(SceUID thid);
int sceKernelExitThread(int status);
int sceKernelExitDeleteThread(int status);
int sceKernelDelayThread(SceUInt32 microseconds);

int sceKernelCreateLwMutex(SceKernelLwMutexWork *work, const char *name, unsigned int attributes,
                           int initial_count, const void *options);
int sceKernelLockLwMutex(SceKernelLwMutexWork *work, int lock_count, unsigned int *timeout);
int sceKernelUnlockLwMutex(SceKernelLwMutexWork *work, int unlock_count);

/* Kernel mutexes, identified by UID (used by ftpvita). */
SceUID sceKernelCreateMutex(const char *name, SceUInt32 attributes, int initial_count,
                            const void *options);
int sceKernelDeleteMutex(SceUID mutex_id);
int sceKernelLockMutex(SceUID mutex_id, int lock_count, unsigned int *timeout);
int sceKernelUnlockMutex(SceUID mutex_id, int unlock_count);

/* Monotonic microseconds. */
SceUInt64 sceKernelGetProcessTimeWide(void);
SceUInt64 sceKernelGetSystemTimeWide(void);

/* Controller (sceCtrl) */

#define SCE_CTRL_SELECT   0x00000001
#define SCE_CTRL_L3       0x00000002
#define SCE_CTRL_R3       0x00000004
#define SCE_CTRL_START    0x00000008
#define SCE_CTRL_UP       0x00000010
#define SCE_CTRL_RIGHT    0x00000020
#define SCE_CTRL_DOWN     0x00000040
#define SCE_CTRL_LEFT     0x00000080
#define SCE_CTRL_LTRIGGER 0x00000100
#define SCE_CTRL_RTRIGGER 0x00000200
#define SCE_CTRL_L1       0x00000400
#define SCE_CTRL_R1       0x00000800
#define SCE_CTRL_TRIANGLE 0x00001000
#define SCE_CTRL_CIRCLE   0x00002000
#define SCE_CTRL_CROSS    0x00004000
#define SCE_CTRL_SQUARE   0x00008000

typedef struct SceCtrlData {
  SceUInt64 timeStamp;
  unsigned int buttons;
  unsigned char lx;
  unsigned char ly;
  unsigned char rx;
  unsigned char ry;
  uint8_t reserved[16];
} SceCtrlData;

/* Reads the merged state of every connected controller (switch2/src/psp2_ctrl.c). */
int sceCtrlPeekBufferPositive(int port, SceCtrlData *pad_data, int count);

/* Display: paces the frame loop to 60 Hz (switch2/src/vita2d_sdl.c). */
int sceDisplayWaitVblankStart(void);

/* System parameters (sceAppUtil): the values VitaShell compares against. */

#define SCE_SYSTEM_PARAM_ENTER_BUTTON_CIRCLE 0
#define SCE_SYSTEM_PARAM_ENTER_BUTTON_CROSS  1

#define SCE_SYSTEM_PARAM_DATE_FORMAT_YYYYMMDD 0
#define SCE_SYSTEM_PARAM_DATE_FORMAT_DDMMYYYY 1
#define SCE_SYSTEM_PARAM_DATE_FORMAT_MMDDYYYY 2

#define SCE_SYSTEM_PARAM_TIME_FORMAT_12HR 0
#define SCE_SYSTEM_PARAM_TIME_FORMAT_24HR 1

/* Common dialog status and VitaShell's own message dialog (uncommon_dialog.c) */

typedef enum SceCommonDialogStatus {
  SCE_COMMON_DIALOG_STATUS_NONE = 0,
  SCE_COMMON_DIALOG_STATUS_RUNNING = 1,
  SCE_COMMON_DIALOG_STATUS_FINISHED = 2,
} SceCommonDialogStatus;

typedef enum SceMsgDialogMode {
  SCE_MSG_DIALOG_MODE_INVALID = 0,
  SCE_MSG_DIALOG_MODE_USER_MSG = 1,
  SCE_MSG_DIALOG_MODE_SYSTEM_MSG = 2,
  SCE_MSG_DIALOG_MODE_ERROR_CODE = 3,
  SCE_MSG_DIALOG_MODE_PROGRESS_BAR = 4,
} SceMsgDialogMode;

typedef enum SceMsgDialogButtonType {
  SCE_MSG_DIALOG_BUTTON_TYPE_OK = 0,
  SCE_MSG_DIALOG_BUTTON_TYPE_YESNO = 1,
  SCE_MSG_DIALOG_BUTTON_TYPE_NONE = 2,
  SCE_MSG_DIALOG_BUTTON_TYPE_OK_CANCEL = 3,
  SCE_MSG_DIALOG_BUTTON_TYPE_CANCEL = 4,
  SCE_MSG_DIALOG_BUTTON_TYPE_3BUTTONS = 5,
} SceMsgDialogButtonType;

typedef enum SceMsgDialogButtonId {
  SCE_MSG_DIALOG_BUTTON_ID_INVALID = 0,
  SCE_MSG_DIALOG_BUTTON_ID_OK = 1,
  SCE_MSG_DIALOG_BUTTON_ID_YES = 1,
  SCE_MSG_DIALOG_BUTTON_ID_NO = 2,
  SCE_MSG_DIALOG_BUTTON_ID_RETRY = 3,
} SceMsgDialogButtonId;

typedef enum SceMsgDialogProgressBarType {
  SCE_MSG_DIALOG_PROGRESSBAR_TYPE_PERCENTAGE = 0,
} SceMsgDialogProgressBarType;

typedef enum SceMsgDialogProgressBarTarget {
  SCE_MSG_DIALOG_PROGRESSBAR_TARGET_BAR_DEFAULT = 0,
} SceMsgDialogProgressBarTarget;

typedef struct SceMsgDialogUserMessageParam {
  SceInt32 buttonType;
  const SceChar8 *msg;
  void *buttonParam;
} SceMsgDialogUserMessageParam;

typedef struct SceMsgDialogProgressBarParam {
  SceInt32 barType;
  const SceChar8 *msg;
} SceMsgDialogProgressBarParam;

typedef struct SceMsgDialogParam {
  SceInt32 mode;
  SceMsgDialogUserMessageParam *userMsgParam;
  SceMsgDialogProgressBarParam *progBarParam;
} SceMsgDialogParam;

typedef struct SceMsgDialogResult {
  SceInt32 mode;
  SceInt32 result;
  SceInt32 buttonId;
} SceMsgDialogResult;

static inline void sceMsgDialogParamInit(SceMsgDialogParam *param) {
  param->mode = SCE_MSG_DIALOG_MODE_INVALID;
  param->userMsgParam = NULL;
  param->progBarParam = NULL;
}

/* Implemented by VitaShell itself in uncommon_dialog.c. */
int sceMsgDialogInit(const SceMsgDialogParam *param);
SceCommonDialogStatus sceMsgDialogGetStatus(void);
int sceMsgDialogClose(void);
int sceMsgDialogTerm(void);
int sceMsgDialogGetResult(SceMsgDialogResult *result);
int sceMsgDialogProgressBarSetMsg(SceMsgDialogProgressBarTarget target, const SceChar8 *barMsg);
int sceMsgDialogProgressBarSetValue(SceMsgDialogProgressBarTarget target, SceUInt32 rate);

/* Text input types passed to initImeDialog (switch2/src/ime_dialog.c) */

#define SCE_IME_TYPE_DEFAULT          0
#define SCE_IME_TYPE_BASIC_LATIN      1
#define SCE_IME_TYPE_NUMBER           2
#define SCE_IME_TYPE_EXTENDED_NUMBER  3
#define SCE_IME_OPTION_MULTILINE      0x00000001
#define SCE_IME_DIALOG_MAX_TITLE_LENGTH 128
#define SCE_IME_DIALOG_MAX_TEXT_LENGTH  2048

/* Texture filters, as vita2d takes them */

typedef enum SceGxmTextureFilter {
  SCE_GXM_TEXTURE_FILTER_POINT = 0,
  SCE_GXM_TEXTURE_FILTER_LINEAR = 1,
} SceGxmTextureFilter;

/* Power and application management: the parts with a Switch meaning */

int scePowerGetBatteryLifePercent(void);
int scePowerIsBatteryCharging(void);
int scePowerIsLowBattery(void);

int sceAppMgrGetDevInfo(const char *device, uint64_t *max_size, uint64_t *free_size);

/* No effect on Switch 2: there is no background music port to claim, and
   the audio player's "screen off" is not ported. */
int sceAppMgrAcquireBgmPort(void);
int sceAppMgrReleaseBgmPort(void);
int scePowerRequestDisplayOff(void);

#ifdef __cplusplus
}
#endif

#endif
