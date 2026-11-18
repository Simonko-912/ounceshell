/*
  VitaShell
  Copyright (C) 2015-2018, TheFloW
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
 * Switch 2 replacement for VitaShell's init.c, keeping its interface
 * (init.h). Derived from upstream init.c: the default theme and language
 * files and the bookmark and recent-file folders are installed the same
 * way, under sdmc:/switch/ounceshell/. Upstream's second theme, Electron, is
 * not installed: its backgrounds paint the Vita's button legends. The Vita's system modules, kernel
 * plugins, network, SQLite, QR camera and updater are not started.
 */

#include "main.h"
#include "browser.h"
#include "init.h"
#include "file.h"
#include "utils.h"

#include "audio/vita_audio.h"

#include <SDL.h>

#include <switch2_platform.h>

INCLUDE_EXTERN_RESOURCE(english_us_txt);

INCLUDE_EXTERN_RESOURCE(theme_txt);

INCLUDE_EXTERN_RESOURCE(default_colors_txt);
INCLUDE_EXTERN_RESOURCE(default_archive_icon_png);
INCLUDE_EXTERN_RESOURCE(default_audio_icon_png);
INCLUDE_EXTERN_RESOURCE(default_battery_bar_charge_png);
INCLUDE_EXTERN_RESOURCE(default_battery_bar_green_png);
INCLUDE_EXTERN_RESOURCE(default_battery_bar_red_png);
INCLUDE_EXTERN_RESOURCE(default_battery_png);
INCLUDE_EXTERN_RESOURCE(default_cover_png);
INCLUDE_EXTERN_RESOURCE(default_fastforward_png);
INCLUDE_EXTERN_RESOURCE(default_fastrewind_png);
INCLUDE_EXTERN_RESOURCE(default_file_icon_png);
INCLUDE_EXTERN_RESOURCE(default_folder_icon_png);
INCLUDE_EXTERN_RESOURCE(default_ftp_png);
INCLUDE_EXTERN_RESOURCE(default_image_icon_png);
INCLUDE_EXTERN_RESOURCE(default_pause_png);
INCLUDE_EXTERN_RESOURCE(default_play_png);
INCLUDE_EXTERN_RESOURCE(default_sfo_icon_png);
INCLUDE_EXTERN_RESOURCE(default_text_icon_png);
INCLUDE_EXTERN_RESOURCE(default_file_symlink_icon_png);
INCLUDE_EXTERN_RESOURCE(default_folder_symlink_icon_png);

/* A file written to the SD card at startup: its embedded contents run from
   start to end. Files marked replace are rewritten when their size differs. */
typedef struct {
  const char *path;
  const unsigned char *start;
  const unsigned char *end;
  int replace;
} EmbeddedDefaultFile;

#define DEFAULT_FILE(path, name, replace) \
  { VITASHELL_DATA_PATH path, &_binary_resources_##name##_start, &_binary_resources_##name##_end, replace }

static const EmbeddedDefaultFile default_files[] = {
  DEFAULT_FILE("language/english_us.txt", english_us_txt, 0),

  DEFAULT_FILE("theme/theme.txt", theme_txt, 0),

  DEFAULT_FILE("theme/Default/colors.txt", default_colors_txt, 1),
  DEFAULT_FILE("theme/Default/archive_icon.png", default_archive_icon_png, 1),
  DEFAULT_FILE("theme/Default/audio_icon.png", default_audio_icon_png, 1),
  DEFAULT_FILE("theme/Default/battery.png", default_battery_png, 1),
  DEFAULT_FILE("theme/Default/battery_bar_charge.png", default_battery_bar_charge_png, 1),
  DEFAULT_FILE("theme/Default/battery_bar_green.png", default_battery_bar_green_png, 1),
  DEFAULT_FILE("theme/Default/battery_bar_red.png", default_battery_bar_red_png, 1),
  DEFAULT_FILE("theme/Default/cover.png", default_cover_png, 1),
  DEFAULT_FILE("theme/Default/fastforward.png", default_fastforward_png, 1),
  DEFAULT_FILE("theme/Default/fastrewind.png", default_fastrewind_png, 1),
  DEFAULT_FILE("theme/Default/file_icon.png", default_file_icon_png, 1),
  DEFAULT_FILE("theme/Default/file_symlink_icon.png", default_file_symlink_icon_png, 1),
  DEFAULT_FILE("theme/Default/folder_icon.png", default_folder_icon_png, 1),
  DEFAULT_FILE("theme/Default/folder_symlink_icon.png", default_folder_symlink_icon_png, 1),
  DEFAULT_FILE("theme/Default/ftp.png", default_ftp_png, 1),
  DEFAULT_FILE("theme/Default/image_icon.png", default_image_icon_png, 1),
  DEFAULT_FILE("theme/Default/pause.png", default_pause_png, 1),
  DEFAULT_FILE("theme/Default/play.png", default_play_png, 1),
  DEFAULT_FILE("theme/Default/sfo_icon.png", default_sfo_icon_png, 1),
  DEFAULT_FILE("theme/Default/text_icon.png", default_text_icon_png, 1),

};

#define N_DEFAULT_FILES (sizeof(default_files) / sizeof(EmbeddedDefaultFile))

char vitashell_titleid[12] = "VITASHELL";

int is_safe_mode = 0;

/* No Vita kernel or user plugins are loaded. */
SceUID kernel_modid = -1, user_modid = -1;

// System params: English, A confirms (set from the settings), 24 hour clock.
int language = 1, enter_button = SCE_SYSTEM_PARAM_ENTER_BUTTON_CIRCLE;
int date_format = SCE_SYSTEM_PARAM_DATE_FORMAT_YYYYMMDD, time_format = SCE_SYSTEM_PARAM_TIME_FORMAT_24HR;

int isSafeMode() {
  return is_safe_mode;
}

vita2d_pgf *loadSystemFonts() {
  return switch2LoadSystemFonts();
}

static void installDefaultFiles() {
  // Make VitaShell folders: sdmc:/switch/ may not exist yet.
  sceIoMkdir("sdmc:/switch", 0777);
  sceIoMkdir(VITASHELL_DATA_PATH, 0777);
  sceIoMkdir(VITASHELL_DATA_PATH "internal", 0777);
  sceIoMkdir(VITASHELL_DATA_PATH "language", 0777);
  sceIoMkdir(VITASHELL_DATA_PATH "theme", 0777);
  sceIoMkdir(VITASHELL_DATA_PATH "theme/Default", 0777);

  // Write default files if they don't exist
  int i;
  for (i = 0; i < N_DEFAULT_FILES; i++) {
    const EmbeddedDefaultFile *file = &default_files[i];
    int size = (int)(file->end - file->start);

    SceIoStat stat;
    memset(&stat, 0, sizeof(stat));
    if (sceIoGetstat(file->path, &stat) < 0 || (file->replace && (int)stat.st_size != size))
      WriteFile(file->path, file->start, size);
  }
}

static void cleanRecentFiles() {
  // Clear up the recent folder every two weeks, as upstream does. The
  // Switch file system reports no times for folders (they read as 1970);
  // then the folder's age is unknown and it is kept.
  SceIoStat stat;
  SceDateTime now;
  sceRtcGetCurrentClock(&now, 0);
  int res = sceIoGetstat(VITASHELL_RECENT_PATH, &stat);
  if (res >= 0 && stat.st_ctime.year > 1970) {
    if (now.year * 365 + now.day - stat.st_ctime.year * 365 - stat.st_ctime.day
        >= VITASHELL_RECENT_PATH_DELETE_INTERVAL_DAYS) {
      removePath(VITASHELL_RECENT_PATH, 0);
    }
  }

  if (!checkFolderExist(VITASHELL_BOOKMARKS_PATH)) {
    sceIoMkdir(VITASHELL_BOOKMARKS_PATH, 0777);
  }
  if (!checkFolderExist(VITASHELL_RECENT_PATH)) {
    sceIoMkdir(VITASHELL_RECENT_PATH, 0777);
  }
  time_last_recent_files = 0;
  time_last_bookmarks = 0;
}

void initVitaShell() {
  // The audio device opens at the first playback (vita_audio_sdl.c).
  vitaAudioInit(0x40);

  if (vita2d_init() < 0)
    fprintf(stderr, "init: no renderer; VitaShell cannot draw\n");

  switch2CtrlInit();
  switch2PowerInit();
  switch2MountNand();

  installDefaultFiles();
  cleanRecentFiles();
}

void finishVitaShell() {
  // Stops the FTP server's threads and closes its sockets and the BSD client.
  ftpvita_fini();

  switch2PowerExit();
  switch2CtrlExit();

  vita2d_free_pgf(font);
  font = NULL;
  vita2d_fini();

  vitaAudioShutdown();

  SDL_Quit();
}
