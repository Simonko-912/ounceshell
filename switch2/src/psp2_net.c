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
 * sceNet and sceNetCtl (vitasdk_net.h) over libounce-nx's BSD sockets and
 * nifm, for ftpvita.
 *
 * Abort: ftpvita stops its threads with sceNetSocketAbort (client sockets)
 * and sceNetSocketClose (the listening socket), and expects their blocked
 * sceNetRecv to return SCE_NET_ERROR_EINTR and sceNetAccept to fail. Any
 * other result makes a client thread take the client-list lock that
 * ftpvita_fini holds while it waits, so the result must be exact. Here a
 * blocking call waits with poll() in short steps and checks an abort flag
 * for its socket between steps; abort and close set the flag. shutdown()
 * is not used for this: it makes recv() return 0, which ftpvita reads as
 * the client disconnecting.
 */

#include <vitasdk_net.h>

#include <switch.h>

#include <errno.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>

/* How long a blocked call waits before it checks its abort flag again. */
#define ABORT_CHECK_INTERVAL_MS 100

/* Abort flags by socket descriptor. Descriptors at or above the table size
   cannot be aborted; their calls block as plain BSD calls. */
#define MAX_TRACKED_SOCKETS 256

static int socket_aborted[MAX_TRACKED_SOCKETS];
static int net_initialized;
static int netctl_initialized;

static int netError(int error) {
  return (int)(SCE_NET_ERROR_BASE | (error & 0xFF));
}

static int resultError(Result rc) {
  return (int)(0x80000000u | (rc & 0x7FFFFFFFu));
}

static void setAborted(int s, int aborted) {
  if (s >= 0 && s < MAX_TRACKED_SOCKETS)
    __atomic_store_n(&socket_aborted[s], aborted, __ATOMIC_SEQ_CST);
}

static int isAborted(int s) {
  if (s < 0 || s >= MAX_TRACKED_SOCKETS)
    return 0;
  return __atomic_load_n(&socket_aborted[s], __ATOMIC_SEQ_CST);
}

/* Waits until the socket is ready for the events, has failed, or is
   aborted. Returns 0 when the caller should make its BSD call, or
   SCE_NET_ERROR_EINTR when the socket was aborted. */
static int waitForSocket(int s, short events) {
  for (;;) {
    if (isAborted(s))
      return SCE_NET_ERROR_EINTR;

    struct pollfd request = { .fd = s, .events = events, .revents = 0 };
    int ready = poll(&request, 1, ABORT_CHECK_INTERVAL_MS);
    if (ready > 0)
      return isAborted(s) ? SCE_NET_ERROR_EINTR : 0;
    if (ready < 0 && errno != EINTR)
      return 0; // The BSD call reports the error.
  }
}

int sceNetInit(SceNetInitParam *param) {
  (void)param;

  if (net_initialized)
    return 0;

  Result rc = socketInitializeDefault();
  if (R_FAILED(rc))
    return resultError(rc);

  net_initialized = 1;
  return 0;
}

int sceNetTerm(void) {
  if (net_initialized) {
    socketExit();
    net_initialized = 0;
  }
  return 0;
}

int sceNetShowNetstat(void) {
  return net_initialized ? 0 : SCE_NET_ERROR_ENOTINIT;
}

int sceNetSocket(const char *name, int domain, int type, int protocol) {
  (void)name;

  int s = socket(domain, type, protocol);
  if (s < 0)
    return netError(errno);

  setAborted(s, 0);
  return s;
}

int sceNetSocketClose(int s) {
  // Set first: a thread blocked on the socket must not see a new socket
  // that reuses the descriptor as its own.
  setAborted(s, 1);
  return close(s) < 0 ? netError(errno) : 0;
}

int sceNetSocketAbort(int s, int flags) {
  (void)flags;
  setAborted(s, 1);
  return 0;
}

int sceNetBind(int s, const SceNetSockaddr *addr, unsigned int addrlen) {
  return bind(s, addr, (socklen_t)addrlen) < 0 ? netError(errno) : 0;
}

int sceNetListen(int s, int backlog) {
  return listen(s, backlog) < 0 ? netError(errno) : 0;
}

int sceNetAccept(int s, SceNetSockaddr *addr, unsigned int *addrlen) {
  int wait = waitForSocket(s, POLLIN);
  if (wait < 0)
    return wait;

  socklen_t length = addrlen ? (socklen_t)*addrlen : 0;
  int client = accept(s, addr, addrlen ? &length : NULL);
  if (client < 0)
    return netError(errno);

  if (addrlen)
    *addrlen = (unsigned int)length;
  setAborted(client, 0);
  return client;
}

int sceNetConnect(int s, const SceNetSockaddr *addr, unsigned int addrlen) {
  return connect(s, addr, (socklen_t)addrlen) < 0 ? netError(errno) : 0;
}

int sceNetGetsockname(int s, SceNetSockaddr *addr, unsigned int *addrlen) {
  socklen_t length = (socklen_t)*addrlen;
  if (getsockname(s, addr, &length) < 0)
    return netError(errno);

  *addrlen = (unsigned int)length;
  return 0;
}

int sceNetSend(int s, const void *msg, unsigned int len, int flags) {
  const unsigned char *data = msg;
  unsigned int sent = 0;

  // ftpvita does not resend the rest of a short send, so send it all here.
  while (sent < len) {
    int wait = waitForSocket(s, POLLOUT);
    if (wait < 0)
      return wait;

    ssize_t result = send(s, data + sent, len - sent, flags);
    if (result < 0) {
      if (errno == EINTR)
        continue;
      return netError(errno);
    }
    sent += (unsigned int)result;
  }

  return (int)sent;
}

int sceNetRecv(int s, void *buf, unsigned int len, int flags) {
  int wait = waitForSocket(s, POLLIN);
  if (wait < 0)
    return wait;

  ssize_t result = recv(s, buf, len, flags);
  if (result < 0)
    return netError(errno);

  return (int)result;
}

unsigned int sceNetHtonl(unsigned int host) {
  return htonl(host);
}

unsigned short int sceNetHtons(unsigned short int host) {
  return htons(host);
}

int sceNetInetPton(int af, const char *src, void *dst) {
  return inet_pton(af, src, dst);
}

const char *sceNetInetNtop(int af, const void *src, char *dst, unsigned int size) {
  return inet_ntop(af, src, dst, (socklen_t)size);
}

/* Network control */

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
