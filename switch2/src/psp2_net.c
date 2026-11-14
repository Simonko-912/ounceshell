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
 * sceNetCtl (vitasdk_net.h) over libounce-nx's nifm.
 */

#include <vitasdk_net.h>

#include <switch.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>

/* Errors are 0x80410100 | errno, as on the Vita. */
#define SCE_NET_ERROR_BASE 0x80410100

static int netctl_initialized;

static int netError(int error) {
  return (int)(SCE_NET_ERROR_BASE | (error & 0xFF));
}

static int resultError(Result rc) {
  return (int)(0x80000000u | (rc & 0x7FFFFFFFu));
}

int sceNetCtlInit(void) {
  if (netctl_initialized)
    return 0;

  Result rc = nifmInitialize(NifmServiceType_User);
  if (R_FAILED(rc))
    return resultError(rc);

  netctl_initialized = 1;
  return 0;
}

void sceNetCtlTerm(void) {
  if (netctl_initialized) {
    nifmExit();
    netctl_initialized = 0;
  }
}

int sceNetCtlInetGetInfo(int code, SceNetCtlInfo *info) {
  if (code != SCE_NETCTL_INFO_GET_IP_ADDRESS || !info)
    return netError(EINVAL);

  // nifm returns the address in network byte order, as in_addr holds it.
  u32 address = 0;
  Result rc = nifmGetCurrentIpAddress(&address);
  if (R_FAILED(rc))
    return resultError(rc);

  struct in_addr in = { .s_addr = address };
  if (!inet_ntop(AF_INET, &in, info->ip_address, sizeof(info->ip_address)))
    return netError(errno);

  return 0;
}

int sceNetCtlInetGetState(int *state) {
  if (!state)
    return netError(EINVAL);

  u32 address = 0;
  Result rc = nifmGetCurrentIpAddress(&address);
  *state = (R_SUCCEEDED(rc) && address != 0) ? SCE_NETCTL_STATE_CONNECTED
                                              : SCE_NETCTL_STATE_DISCONNECTED;
  return 0;
}
