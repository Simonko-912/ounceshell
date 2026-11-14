#pragma once

/* ftpd v2.3.1 (https://github.com/mtheall/ftpd, public domain), modified
 * for OunceShell (Switch 2): see README.md. */

#ifdef __SWITCH2__
/* The port the server listens on, VitaShell's. */
#define FTP_LISTEN_PORT 1337
/* The status line's name, which ftpd's Makefile defines. */
#define STATUS_STRING "ftpd"
#endif

/*! Loop status */
typedef enum
{
  LOOP_CONTINUE, /*!< Continue looping */
  LOOP_RESTART,  /*!< Reinitialize */
  LOOP_EXIT,     /*!< Terminate looping */
} loop_status_t;

int           ftp_init(void);
loop_status_t ftp_loop(void);
void          ftp_exit(void);
