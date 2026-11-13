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
 * The subset of the Vita SDK's network interface (sceNet, sceNetCtl) that
 * ftpvita uses, reimplemented in switch2/src/psp2_net.c on libounce-nx's
 * BSD sockets and nifm. Written from ftpvita's call sites and vitasdk's
 * public documentation. Kept out of vitasdk.h so that the BSD socket
 * headers reach only the files that use the network.
 *
 * The socket address types are the BSD ones: Horizon's sockaddr_in has the
 * Vita's layout (sin_len, sin_family, sin_port, sin_addr), so ftpvita's
 * casts between SceNetSockaddrIn and SceNetSockaddr stay valid.
 */

#ifndef VITASHELL_SWITCH2_VITASDK_NET_H
#define VITASHELL_SWITCH2_VITASDK_NET_H

#include <vitasdk.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCE_NET_AF_INET     AF_INET
#define SCE_NET_SOCK_STREAM SOCK_STREAM
#define SCE_NET_INADDR_ANY  INADDR_ANY

#define SCE_NET_SOCKET_ABORT_FLAG_RCV_PRESERVATION 0x00000001
#define SCE_NET_SOCKET_ABORT_FLAG_SND_PRESERVATION 0x00000002

/* Errors are 0x80410100 | errno, as on the Vita. */
#define SCE_NET_ERROR_BASE      0x80410100
#define SCE_NET_ERROR_EINTR     ((int)0x80410104)
#define SCE_NET_ERROR_ENOTINIT  ((int)0x804101C8)

typedef struct in_addr SceNetInAddr;
typedef struct sockaddr SceNetSockaddr;
typedef struct sockaddr_in SceNetSockaddrIn;

/* sceNetInit's memory pool is not used: the BSD client has its own. */
typedef struct SceNetInitParam {
  void *memory;
  int size;
  int flags;
} SceNetInitParam;

int sceNetInit(SceNetInitParam *param);
int sceNetTerm(void);
/* 0 when sceNetInit has run, else SCE_NET_ERROR_ENOTINIT. */
int sceNetShowNetstat(void);

int sceNetSocket(const char *name, int domain, int type, int protocol);
int sceNetSocketClose(int s);
/* Makes blocked and later sceNetAccept/sceNetRecv/sceNetSend calls on the
   socket return SCE_NET_ERROR_EINTR. The flags are accepted and ignored. */
int sceNetSocketAbort(int s, int flags);
int sceNetBind(int s, const SceNetSockaddr *addr, unsigned int addrlen);
int sceNetListen(int s, int backlog);
int sceNetAccept(int s, SceNetSockaddr *addr, unsigned int *addrlen);
int sceNetConnect(int s, const SceNetSockaddr *addr, unsigned int addrlen);
int sceNetGetsockname(int s, SceNetSockaddr *addr, unsigned int *addrlen);
/* Sends the whole buffer unless the socket fails or is aborted. */
int sceNetSend(int s, const void *msg, unsigned int len, int flags);
int sceNetRecv(int s, void *buf, unsigned int len, int flags);

unsigned int sceNetHtonl(unsigned int host);
unsigned short int sceNetHtons(unsigned short int host);
int sceNetInetPton(int af, const char *src, void *dst);
const char *sceNetInetNtop(int af, const void *src, char *dst, unsigned int size);

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
