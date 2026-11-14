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
 * The FTP server (ftpvita.h): ftpd's server (switch2/third_party/ftpd) on a
 * thread of its own.
 *
 * ftpd never waits in a socket call: each ftp_loop() checks the listening
 * socket and every connection without waiting and handles what is ready,
 * so one thread serves all connections. The thread sleeps for a
 * millisecond between loops. ftpd initializes the socket driver itself
 * and closes it in ftp_exit().
 *
 * ftpd prints its progress to a console on the screen; OunceShell has none,
 * so the console functions it calls do nothing here.
 */

#include <ftpvita.h>

#include "console.h"
#include "ftp.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* How long the server thread sleeps between loops. */
#define LOOP_INTERVAL_NS 1000000L

/* How long the thread waits before it starts the server again, after the
   network went down. */
#define RESTART_INTERVAL_S 1

static pthread_t server_thread;
static int server_thread_started;
static volatile int stop_requested;
static volatile int serving;

static void sleepFor(time_t seconds, long nanoseconds) {
  struct timespec interval = { .tv_sec = seconds, .tv_nsec = nanoseconds };
  nanosleep(&interval, NULL);
}

static void *serve(void *argument) {
  (void)argument;

  while (!stop_requested) {
    loop_status_t status = ftp_loop();

    if (status == LOOP_EXIT)
      break;

    if (status == LOOP_RESTART) {
      // The network went down: start again once it is back.
      ftp_exit();
      while (!stop_requested && ftp_init() != 0)
        sleepFor(RESTART_INTERVAL_S, 0);
      if (stop_requested)
        break;
      continue;
    }

    sleepFor(0, LOOP_INTERVAL_NS);
  }

  ftp_exit();
  serving = 0;
  return NULL;
}

static void joinServerThread(void) {
  if (!server_thread_started)
    return;

  stop_requested = 1;
  pthread_join(server_thread, NULL);
  server_thread_started = 0;
}

int ftpvita_init(char *vita_ip, unsigned short int *vita_port) {
  if (ftpvita_is_initialized())
    return -1;

  // A server that stopped by itself still has its thread to join.
  joinServerThread();

  // The network state comes from nifm, which stays open while the server
  // may run, as with libftpvita. VitaShell's wait dialog also reads it.
  if (sceNetCtlInit() < 0)
    return -1;

  int state = SCE_NETCTL_STATE_DISCONNECTED;
  sceNetCtlInetGetState(&state);
  if (state != SCE_NETCTL_STATE_CONNECTED)
    return -1;

  SceNetCtlInfo info;
  if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info) < 0)
    return -1;

  if (ftp_init() != 0)
    return -1;

  stop_requested = 0;
  serving = 1;
  if (pthread_create(&server_thread, NULL, serve, NULL) != 0) {
    serving = 0;
    ftp_exit();
    return -1;
  }
  server_thread_started = 1;

  snprintf(vita_ip, 16, "%s", info.ip_address);
  *vita_port = FTP_LISTEN_PORT;
  return 0;
}

void ftpvita_fini(void) {
  joinServerThread();
  sceNetCtlTerm();
}

int ftpvita_is_initialized(void) {
  return server_thread_started && serving;
}

/* ftpd's console (console.h). */

void console_init(void) {
}

void console_set_status(const char *format, ...) {
  (void)format;
}

void console_print(const char *format, ...) {
  (void)format;
}

void debug_print(const char *format, ...) {
  (void)format;
}

void console_render(void) {
}
