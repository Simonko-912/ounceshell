# ftpd

The FTP server that VitaShell starts with SELECT (the - button), in place
of libftpvita. `switch2/src/ftp_server.c` runs it.

- Origin: `https://github.com/mtheall/ftpd`, tag `v2.3.1` (commit
  `00c2c44271510927add49e5abae434a03b42fc5c`).
- Files: `source/ftp.c`, `include/ftp.h`, `include/console.h`, `LICENSE`.
- License: public domain (the Unlicense, `LICENSE`).

Changes, each marked "Modified for OunceShell" or under `__SWITCH2__`:

- It listens on port 1337 (`FTP_LISTEN_PORT` in `ftp.h`), VitaShell's.
- `ftp.h` defines `STATUS_STRING`, which ftpd's Makefile defined.
- The socket configuration omits `bsdsockets_version`, which libounce's
  `SocketInitConfig` does not have, and names the BSD session count and
  service.
- `ftp_loop` does not read the controller (B ended ftpd), and no applet
  hook is registered: OunceShell handles input and stops the server.
- `ftp_exit` resets the listening socket, so the server can start again.
- Control and data sockets set `TCP_NODELAY`. Without it, the last small
  packet of each reply, listing or file waited for the client's delayed
  acknowledgement (40 ms on Linux, up to 200 ms on Windows).
- A listing takes each entry's type and size from the directory read, as
  ftpd does on the 3DS, and asks only for a file's modification time,
  converted as libounce's `stat()` converts it
  (`ftp_switch2_entry_stat`). `stat()` made eight requests to the
  console's services per file. The listing output is unchanged; a listing
  of 80 entries took 58 ms and now takes 33 ms.

ftpd prints to a console of its own; `ftp_server.c` supplies the console
functions (`console.h`), which print nothing.
