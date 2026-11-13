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
 * The state of pfs.c ("Open decrypted" mounts of Vita game data), which is
 * not built for Switch 2. The browser and context menu read it in many
 * places; with nothing ever mounted, those checks are always false. The
 * menu entry that would mount is hidden (main_context.c).
 */

#include "main.h"
#include "pfs.h"

#include <switch2_platform.h>

char pfs_mounted_path[MAX_PATH_LENGTH];
char pfs_mount_point[MAX_MOUNT_POINT_LENGTH];
int read_only = 0;

int pfsMount(const char *path) {
  (void)path;
  return SWITCH2_ERROR_NOT_SUPPORTED;
}

int pfsUmount() {
  pfs_mounted_path[0] = '\0';
  pfs_mount_point[0] = '\0';
  return 0;
}
