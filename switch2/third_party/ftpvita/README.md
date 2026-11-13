# libftpvita

The FTP server that VitaShell starts with SELECT. On the Vita it comes from
vitasdk's package set; the Switch 2 port compiles it from source.

- Origin: `https://github.com/xerpi/libftpvita`, commit
  `77a1d39c1d6f11a55fe65a6ed9fe707bff5ede4b`, directory `libftpvita/`.
- Files: `ftpvita.c`, `ftpvita.h`, unchanged.
- License: MIT (`LICENSE`), Copyright (c) 2015-2019 Sergi Granell.

ftpvita calls the Vita SDK's `sceNet*`, `sceNetCtl*`, `sceIo*`,
`sceKernel*` and `sceRtc*` functions. The port supplies them:
`switch2/src/psp2_net.c` (BSD sockets and nifm),
`switch2/src/psp2_io.c`, `switch2/src/psp2_kernel.c` and
`switch2/src/psp2_rtc.c`.
