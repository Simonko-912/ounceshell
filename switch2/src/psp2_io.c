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
 * sceIo* over newlib's POSIX file API, which libounce-nx routes to the
 * Switch file system through its fsdev devices ("sdmc:/..."). Errors are
 * returned as the Vita does, 0x80010000 | errno, so VitaShell's checks for
 * SCE_ERROR_ERRNO_EEXIST keep working.
 *
 * Vita device roots have no slash ("ux0:data"); a path whose colon is not
 * followed by '/' gets one, so "sdmc:switch" means "sdmc:/switch" rather
 * than a path relative to the device's current directory.
 */

#include <vitasdk.h>
#include <switch.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define PATH_BUFFER_SIZE 1280

/* Directory handles share the SceUID space with file descriptors. */
#define DIRECTORY_HANDLE_BASE 0x40000000
#define MAX_OPEN_DIRECTORIES 64

typedef struct {
  DIR *dir;
  char path[PATH_BUFFER_SIZE];
} OpenDirectory;

static OpenDirectory open_directories[MAX_OPEN_DIRECTORIES];
static pthread_mutex_t open_directories_lock = PTHREAD_MUTEX_INITIALIZER;

static int errnoResult(void) {
  return SCE_ERROR_FROM_ERRNO(errno ? errno : EIO);
}

static void normalizePath(const char *path, char *out, size_t out_size) {
  const char *colon = strchr(path, ':');
  if (colon && colon[1] != '/') {
    size_t device_length = (size_t)(colon - path) + 1;
    snprintf(out, out_size, "%.*s/%s", (int)device_length, path, colon + 1);
  } else {
    snprintf(out, out_size, "%s", path);
  }
}

/* Mount the built-in user storage (the NAND) as the fsdev device "nand",
 * so the browser and the FTP server see it next to the SD card ("sdmc").
 * The libounce runtime mounts "sdmc" itself at startup; the built-in
 * storage needs a separate mount: the fs service opens it as the user
 * content storage (the storage Switch 2 installs games and saves to),
 * and fsdev gives it a stdio name. Without it, or when opening fails,
 * OunceShell simply runs on the SD card alone. The runtime's own
 * shutdown (fsdevUnmountAll) unmounts it again. */
void switch2MountNand(void) {
  FsFileSystem fs;
  Result rc = fsOpenContentStorageFileSystem(&fs, FsContentStorageId_User);
  if (R_SUCCEEDED(rc))
    fsdevMountDevice("nand", fs);
}

/* True for "dev:/" and "dev:", which stat may not describe. */
static int isDeviceRoot(const char *normalized_path) {
  const char *colon = strchr(normalized_path, ':');
  return colon && (colon[1] == '\0' || (colon[1] == '/' && colon[2] == '\0'));
}

static void timeToDateTime(time_t value, SceDateTime *date_time) {
  sceRtcSetTime_t(date_time, value);
}

static void statToSceStat(const struct stat *posix_stat, SceIoStat *stat) {
  memset(stat, 0, sizeof(SceIoStat));

  if (S_ISDIR(posix_stat->st_mode))
    stat->st_mode = SCE_S_IFDIR;
  else
    stat->st_mode = SCE_S_IFREG;

  // POSIX and the Vita use the same values for the permission bits.
  stat->st_mode |= posix_stat->st_mode & 0777;
  if ((stat->st_mode & 0777) == 0)
    stat->st_mode |= 0777;

  stat->st_size = S_ISDIR(posix_stat->st_mode) ? 0 : (SceOff)posix_stat->st_size;
  timeToDateTime(posix_stat->st_ctim.tv_sec, &stat->st_ctime);
  timeToDateTime(posix_stat->st_atim.tv_sec, &stat->st_atime);
  timeToDateTime(posix_stat->st_mtim.tv_sec, &stat->st_mtime);
}

static void directoryStat(SceIoStat *stat) {
  memset(stat, 0, sizeof(SceIoStat));
  stat->st_mode = SCE_S_IFDIR | 0777;
}

static int openFlagsToPosix(int flags) {
  int posix_flags;

  if ((flags & SCE_O_RDWR) == SCE_O_RDWR)
    posix_flags = O_RDWR;
  else if (flags & SCE_O_WRONLY)
    posix_flags = O_WRONLY;
  else
    posix_flags = O_RDONLY;

  if (flags & SCE_O_APPEND)
    posix_flags |= O_APPEND;
  if (flags & SCE_O_CREAT)
    posix_flags |= O_CREAT;
  if (flags & SCE_O_TRUNC)
    posix_flags |= O_TRUNC;
  if (flags & SCE_O_EXCL)
    posix_flags |= O_EXCL;

  return posix_flags;
}

SceUID sceIoOpen(const char *path, int flags, SceMode mode) {
  char normalized[PATH_BUFFER_SIZE];
  normalizePath(path, normalized, sizeof(normalized));

  int fd = open(normalized, openFlagsToPosix(flags), mode ? mode : 0777);
  if (fd < 0)
    return errnoResult();

  return fd;
}

int sceIoClose(SceUID fd) {
  if (close(fd) < 0)
    return errnoResult();
  return 0;
}

int sceIoRead(SceUID fd, void *data, SceSize size) {
  ssize_t result = read(fd, data, size);
  if (result < 0)
    return errnoResult();
  return (int)result;
}

int sceIoWrite(SceUID fd, const void *data, SceSize size) {
  ssize_t result = write(fd, data, size);
  if (result < 0)
    return errnoResult();
  return (int)result;
}

SceOff sceIoLseek(SceUID fd, SceOff offset, int whence) {
  off_t result = lseek(fd, (off_t)offset, whence);
  if (result < 0)
    return errnoResult();
  return (SceOff)result;
}

int sceIoLseek32(SceUID fd, int offset, int whence) {
  return (int)sceIoLseek(fd, offset, whence);
}

int sceIoSyncByFd(SceUID fd, int flags) {
  (void)flags;
  if (fsync(fd) < 0)
    return errnoResult();
  return 0;
}

int sceIoRemove(const char *path) {
  char normalized[PATH_BUFFER_SIZE];
  normalizePath(path, normalized, sizeof(normalized));

  if (unlink(normalized) < 0)
    return errnoResult();
  return 0;
}

int sceIoRename(const char *old_path, const char *new_path) {
  char normalized_old[PATH_BUFFER_SIZE];
  char normalized_new[PATH_BUFFER_SIZE];
  normalizePath(old_path, normalized_old, sizeof(normalized_old));
  normalizePath(new_path, normalized_new, sizeof(normalized_new));

  // The Vita refuses to replace an existing entry; VitaShell relies on
  // EEXIST to decide between replacing and merging.
  struct stat existing;
  if (strcasecmp(normalized_old, normalized_new) != 0 && stat(normalized_new, &existing) == 0)
    return SCE_ERROR_FROM_ERRNO(EEXIST);

  if (rename(normalized_old, normalized_new) < 0)
    return errnoResult();
  return 0;
}

int sceIoMkdir(const char *path, SceMode mode) {
  char normalized[PATH_BUFFER_SIZE];
  normalizePath(path, normalized, sizeof(normalized));

  if (mkdir(normalized, mode ? mode : 0777) < 0)
    return errnoResult();
  return 0;
}

int sceIoRmdir(const char *path) {
  char normalized[PATH_BUFFER_SIZE];
  normalizePath(path, normalized, sizeof(normalized));

  if (rmdir(normalized) < 0)
    return errnoResult();
  return 0;
}

static OpenDirectory *findDirectory(SceUID fd) {
  int index = fd - DIRECTORY_HANDLE_BASE;
  if (index < 0 || index >= MAX_OPEN_DIRECTORIES)
    return NULL;
  if (!open_directories[index].dir)
    return NULL;
  return &open_directories[index];
}

SceUID sceIoDopen(const char *path) {
  char normalized[PATH_BUFFER_SIZE];
  normalizePath(path, normalized, sizeof(normalized));

  DIR *dir = opendir(normalized);
  if (!dir)
    return errnoResult();

  pthread_mutex_lock(&open_directories_lock);

  int index;
  for (index = 0; index < MAX_OPEN_DIRECTORIES; index++) {
    if (!open_directories[index].dir)
      break;
  }

  if (index == MAX_OPEN_DIRECTORIES) {
    pthread_mutex_unlock(&open_directories_lock);
    closedir(dir);
    return SCE_ERROR_FROM_ERRNO(EMFILE);
  }

  open_directories[index].dir = dir;
  snprintf(open_directories[index].path, sizeof(open_directories[index].path), "%s", normalized);

  pthread_mutex_unlock(&open_directories_lock);

  return DIRECTORY_HANDLE_BASE + index;
}

/* Returns 1 with an entry, 0 at the end, or an error. */
int sceIoDread(SceUID fd, SceIoDirent *entry) {
  OpenDirectory *directory = findDirectory(fd);
  if (!directory)
    return SCE_ERROR_FROM_ERRNO(EBADF);

  struct dirent *posix_entry;
  do {
    errno = 0;
    posix_entry = readdir(directory->dir);
    if (!posix_entry)
      return errno ? errnoResult() : 0;
  } while (strcmp(posix_entry->d_name, ".") == 0 || strcmp(posix_entry->d_name, "..") == 0);

  memset(entry, 0, sizeof(SceIoDirent));
  snprintf(entry->d_name, sizeof(entry->d_name), "%s", posix_entry->d_name);

  char entry_path[PATH_BUFFER_SIZE + 256];
  size_t length = strlen(directory->path);
  int has_slash = length > 0 && directory->path[length - 1] == '/';
  snprintf(entry_path, sizeof(entry_path), "%s%s%s", directory->path, has_slash ? "" : "/",
           posix_entry->d_name);

  struct stat posix_stat;
  if (stat(entry_path, &posix_stat) == 0) {
    statToSceStat(&posix_stat, &entry->d_stat);
  } else if (posix_entry->d_type == DT_DIR) {
    directoryStat(&entry->d_stat);
  } else {
    entry->d_stat.st_mode = SCE_S_IFREG | 0777;
  }

  return 1;
}

int sceIoDclose(SceUID fd) {
  pthread_mutex_lock(&open_directories_lock);

  OpenDirectory *directory = findDirectory(fd);
  if (!directory) {
    pthread_mutex_unlock(&open_directories_lock);
    return SCE_ERROR_FROM_ERRNO(EBADF);
  }

  DIR *dir = directory->dir;
  directory->dir = NULL;
  directory->path[0] = '\0';

  pthread_mutex_unlock(&open_directories_lock);

  closedir(dir);
  return 0;
}

int sceIoGetstat(const char *path, SceIoStat *stat_out) {
  char normalized[PATH_BUFFER_SIZE];
  normalizePath(path, normalized, sizeof(normalized));

  struct stat posix_stat;
  if (stat(normalized, &posix_stat) == 0) {
    statToSceStat(&posix_stat, stat_out);
    return 0;
  }

  int saved_errno = errno;

  // The file system may not describe its own root; it is a directory if
  // it can be listed.
  if (isDeviceRoot(normalized)) {
    DIR *dir = opendir(normalized);
    if (dir) {
      closedir(dir);
      directoryStat(stat_out);
      return 0;
    }
  }

  errno = saved_errno;
  return errnoResult();
}

int sceIoGetstatByFd(SceUID fd, SceIoStat *stat_out) {
  OpenDirectory *directory = findDirectory(fd);
  if (directory)
    return sceIoGetstat(directory->path, stat_out);

  struct stat posix_stat;
  if (fstat(fd, &posix_stat) < 0)
    return errnoResult();

  statToSceStat(&posix_stat, stat_out);
  return 0;
}

/* The Switch file system keeps its own timestamps and has no permission
   bits, so changing them succeeds without an effect. */
int sceIoChstat(const char *path, SceIoStat *stat, int bits) {
  (void)path;
  (void)stat;
  (void)bits;
  return 0;
}

int sceIoChstatByFd(SceUID fd, const SceIoStat *stat, unsigned int bits) {
  (void)fd;
  (void)stat;
  (void)bits;
  return 0;
}

static int getDeviceInfo(const char *device, SceOff *max_size, SceOff *free_size,
                         SceSize *cluster_size) {
  char normalized[PATH_BUFFER_SIZE];
  normalizePath(device, normalized, sizeof(normalized));

  struct statvfs info;
  if (statvfs(normalized, &info) < 0)
    return errnoResult();

  uint64_t fragment_size = info.f_frsize ? info.f_frsize : info.f_bsize;
  *max_size = (SceOff)((uint64_t)info.f_blocks * fragment_size);
  *free_size = (SceOff)((uint64_t)info.f_bfree * fragment_size);
  if (cluster_size)
    *cluster_size = (SceSize)info.f_bsize;
  return 0;
}

int sceIoDevctl(const char *device, unsigned int command, void *input, int input_size,
                void *output, int output_size) {
  (void)input;
  (void)input_size;

  if (command != SCE_IO_DEVCTL_GET_DEVICE_INFO || !output ||
      output_size < (int)sizeof(SceIoDevInfo))
    return SCE_ERROR_FROM_ERRNO(EINVAL);

  SceIoDevInfo *info = output;
  memset(info, 0, sizeof(SceIoDevInfo));
  return getDeviceInfo(device, &info->max_size, &info->free_size, &info->cluster_size);
}

int sceAppMgrGetDevInfo(const char *device, uint64_t *max_size, uint64_t *free_size) {
  SceOff max = 0, free_space = 0;
  int result = getDeviceInfo(device, &max, &free_space, NULL);
  if (result < 0)
    return result;

  *max_size = (uint64_t)max;
  *free_size = (uint64_t)free_space;
  return 0;
}
