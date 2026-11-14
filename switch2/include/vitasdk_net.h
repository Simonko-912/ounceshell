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
 * The subset of the Vita SDK's network control interface (sceNetCtl) that
 * VitaShell uses, reimplemented in switch2/src/psp2_net.c on libounce-nx's
 * nifm. Written from VitaShell's call sites and vitasdk's public
 * documentation. The FTP server (ftpvita.h) uses BSD sockets directly.
 */

#ifndef VITASHELL_SWITCH2_VITASDK_NET_H
#define VITASHELL_SWITCH2_VITASDK_NET_H

#include <vitasdk.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Network control (sceNetCtl) over nifm. */

#define SCE_NETCTL_INFO_GET_IP_ADDRESS 15
#define SCE_NETCTL_STATE_DISCONNECTED  0
#define SCE_NETCTL_STATE_CONNECTED     3

typedef union SceNetCtlInfo {
  char ip_address[16];
} SceNetCtlInfo;

int sceNetCtlInit(void);
void sceNetCtlTerm(void);
/* Supports SCE_NETCTL_INFO_GET_IP_ADDRESS only. */
int sceNetCtlInetGetInfo(int code, SceNetCtlInfo *info);
int sceNetCtlInetGetState(int *state);

#ifdef __cplusplus
}
#endif

#endif
