# OunceShell: VitaShell for Switch 2

OunceShell is a port of [VitaShell](https://github.com/TheOfficialFloW/VitaShell),
the PS Vita file manager by TheFloW, to a native Switch 2 homebrew NRO built with the
Switch 2 devkit ([ouncebrew/ounce-devkit](https://github.com/ouncebrew/ounce-devkit)):
libounce (the Switch 2 reimplementation of libnx), OunceVK's
EGL and OpenGL ES, and devkitPro's SDL 2 portlibs.

**Status: it runs on a Switch 2.** On 10 November 2026 it started from
hbmenu and showed the `sdmc:/` listing with sizes and dates, the clock and
the battery (charging). Folder navigation and the text viewer worked, and a
file made with New file and the built-in keyboard was listed and opened.
With the SDK build, New file opened the Switch software keyboard and
created the file, and SELECT started the FTP server (see Features).
Settings > Exit returned to hbmenu.
The other features have not been tried on the console yet; see
[Not yet tested on hardware](#not-yet-tested-on-hardware).

- Upstream: `https://github.com/TheOfficialFloW/VitaShell`, branch
  `master`, commit `81af70971ba18b8ce86215b04180f1e3d21cdfc9`
  (2022-09-05, VitaShell 2.02).
- Output: `build/switch2/ounceshell.nro`. Its homebrew menu icon is
  `switch2/resources/icon.jpg`, a 256x256 folder.

## Building

With the Switch 2 devkit
([ouncebrew/ounce-devkit](https://github.com/ouncebrew/ounce-devkit))
installed, from the repository root:

```sh
make -f switch2/Makefile            # -> build/switch2/ounceshell.nro
```

or in the devkit's Docker image:

```sh
docker run --rm -v "$PWD:/src" -w /src ounce-devkit make -f switch2/Makefile
```

The devkit has devkitA64, libounce in `$DEVKITPRO/libounce`, OunceVK in
`$DEVKITPRO/portlibs/switch2` and devkitPro's Switch portlibs.

The Makefile embeds the resource files with `ld -r -b binary` as
upstream's CMake build does, links the ELF, and makes the NRO with
`nacptool` and `elf2nro`. The link searches `portlibs/switch2`, then
libounce, then `portlibs/switch`: OunceVK's `libEGL` comes before
switch-mesa's, and libounce's `libnx` before devkitPro's (`sdl2-config` is
not used, because it adds devkitPro's libnx directory). Compiler flags:
`-std=gnu11` (the code predates C23's `bool` keywords), `-funsigned-char`
(as on the Vita; see the text viewer fix below), upstream's warning
switches, `-D__SWITCH__ -D__SWITCH2__`.

## Installing and files on the SD card

Copy `ounceshell.nro` to `sdmc:/switch/ounceshell/ounceshell.nro` (any
folder works) and start it from the homebrew menu. It keeps its data in
`sdmc:/switch/ounceshell/`, as VitaShell kept `ux0:VitaShell/` on the Vita:

| Path | Contents |
| --- | --- |
| `settings.txt` | settings (`ENTER_BUTTON`) |
| `theme/theme.txt`, `theme/Default/` | the theme, written at startup as upstream does; upstream's Electron theme is not installed, as its backgrounds paint the Vita's button legends |
| `language/english_us.txt` | the English strings, editable |
| `internal/lastdir.txt` | the last folder, reopened at start |
| `bookmarks/`, `recent/` | bookmark and recent-file links |

Holding L at start ignores the custom configuration, as upstream does.

## Design

VitaShell's UI code is written against vita2d and a set of Vita SDK
(`sce*`) calls. The port keeps that code and supplies those interfaces:
`switch2/include` has headers with the Vita names (`vitasdk.h`,
`vita2d.h`, `psp2/...`) declaring only what VitaShell uses, and
`switch2/src` implements them on libounce-nx, newlib and SDL 2. Upstream
files change only behind `#ifdef __SWITCH2__` (plus the portable fixes
listed below). No Sony SDK or firmware code is used: the headers are
written from VitaShell's call sites and vitasdk's public documentation.

### What replaced each Vita interface

| Vita interface | Switch 2 replacement | File |
| --- | --- | --- |
| vita2d drawing (rectangles, textures, part/scale/rotate, clipping, clear, swap) | SDL 2 renderer (GLES 2 on EGL/Zink/NXVK), logical size 960x544 | `vita2d_sdl.c` |
| vita2d PNG/JPEG/BMP loading | SDL_image, read through an sceIo stream | `vita2d_sdl.c` |
| vita2d PGF fonts (`vita2d_pgf_*`, `vita2d_load_system_pgf`, `vita2d_load_custom_pgf`) | SDL_ttf over FreeType: the pl:u shared fonts, then bundled Noto Sans; a theme's `font.ttf`/`font.otf` in place of `font.pgf` | `vita2d_font.c` |
| `sceIo*` (files, directories, stat, devctl free space) | newlib POSIX calls on libounce-nx's fsdev (`sdmc:`), statvfs; errors as `0x80010000 \| errno` | `psp2_io.c` |
| `sceKernel*` threads, `ExitDeleteThread`, `WaitThreadEnd`, LwMutex, delays, clocks | pthreads (libounce-nx Horizon threads), argument blocks copied as on the Vita; `svcSleepThread`; system tick | `psp2_kernel.c` |
| `sceRtc*` | civil-date arithmetic; wall clock from `time()`, local zone from newlib's TZ (set by libounce-nx from the time service) | `psp2_rtc.c` |
| `sceCtrlPeekBufferPositive` | every SDL joystick merged (one per player slot) | `psp2_ctrl.c` |
| `sceDisplayWaitVblankStart` | vsynced present, plus a 60 Hz floor if the swap interval is not honoured | `vita2d_sdl.c` |
| `sceMsgDialog*` | unchanged: VitaShell draws its own (`uncommon_dialog.c`) | upstream |
| `sceImeDialog*` (`ime_dialog.c`) | the software keyboard library applet (swkbd) | `ime_dialog.c` (replaces upstream's) |
| `sceAudioOut*` channel thread (`audio/vita_audio.c`) | SDL audio callback pulling 1024-frame blocks from the player; device opens at first playback | `vita_audio_sdl.c` (replaces upstream's) |
| `scePower*` battery | psm (`psmGetBatteryChargePercentage`, `psmGetChargerType`); hidden if the service fails | `psp2_misc.c` |
| `sceAppUtil*`, `sceSysmodule*`, `sceShellUtil*`, taiHEN, kernel plugins, `sceNet*`, `sceHttp*`, `sceSsl*`, SQLite | not used | `init.c` (replaces upstream's) |
| Onigmo POSIX regex (lyrics) | Oniguruma's POSIX API (portlib) | `include/onigmoposix.h` |

Every `sce*` function VitaShell calls is either implemented above or its
call site is excluded with `#ifdef __SWITCH2__` (see the feature table).

### Rendering and resolution

VitaShell's layout is in the Vita's 960x544 pixels. The port keeps that
layout and lets SDL scale it with `SDL_RenderSetLogicalSize(960, 544)`.
The window takes the console's default resolution
(`appletGetDefaultDisplayResolution`): 1920x1080 handheld, the TV output
setting docked. At 1080p that is a 1.99x scale with a 7-pixel bar on each
side; at 2160p, 3.97x. It follows docking and undocking: on AM's
resolution-change event the window is resized, SDL's Switch backend
recreates the EGL surface, and each font rasterizes again at the new scale
when it next draws. Text stays sharp: glyphs are rasterized at the output
resolution (VitaShell's 20 px em times the scale) into 1024x1024 atlas
textures, drawn on whole output pixels with nearest filtering. VitaShell
clips text rows to 23 pixels, sized for the PGF fonts; the port lets the
clip extend 6 pixels lower for Noto Sans's and the shared fonts'
descenders. Images and the theme's bitmaps are drawn with linear
filtering. Drawing is on the GPU: SDL's OpenGL ES 2 renderer, through
OunceVK's EGL and OpenGL (Zink on OunceVK). If that renderer cannot be
created, SDL's software renderer is tried.

Text: each character is drawn with the first face that has it (shared
fonts in the order pl:u returns them for `en-US`, then Noto Sans), so
Latin, CJK and the Nintendo extension font's controller symbols come from
the system. Without a face for a symbol, the button symbols fall back to
letters (A, B, X, Y, L, R, +, -) and the menu arrows to `<` `>`. Glyph
metrics are cached on first use; pixels are rendered only by the drawing
(main) thread, because VitaShell's worker threads measure text when they
open dialogs. All font operations hold a per-font lock.

Text is drawn one logical pixel right of its position: VitaShell clips file
names one pixel right of where it draws them, which the PGF glyphs' left
bearing allowed for.

### Input

Face buttons map by position. VitaShell's own "enter button" setting
(Settings, "Confirm button") decides whether A or B confirms; A is the
default, as on the Switch.

| Vita | Switch 2 | VitaShell use (A confirms) |
| --- | --- | --- |
| circle (right) | A | open / confirm |
| cross (bottom) | B | back / cancel |
| triangle (top) | X | context menu |
| square (left) | Y | mark file |
| L / R | L or ZL / R or ZR | page up/down, viewers |
| START | + | settings |
| SELECT | - | start the FTP server (was USB or FTP) |
| D-pad, left stick | D-pad, left stick | move |
| right stick | right stick | photo zoom |

Shortcuts kept from upstream: D-pad left + Y (square) opens bookmarks,
D-pad left + X (triangle) the recent files. The dialogs show the Switch
button symbols. Touch input is not ported.

### Text input

`ime_dialog.c` keeps upstream's interface (`initImeDialog`,
`updateImeDialog`, ...) and opens the Switch software keyboard (libounce's
`swkbdShow`), as upstream opened the Vita's system keyboard. A cancel
there cancels; so does a keyboard that cannot start.

### File system

The home list shows one device, `sdmc:/`. Upstream builds paths by string
concatenation from device names like `ux0:`; with `sdmc:/` as the device
name the same code yields `sdmc:/switch/...`. `browser.c`'s
`change_to_directory` treats `:/` as one level. `psp2_io.c` also turns any
`dev:path` into `dev:/path`, so no path is taken as relative to a device's
current directory. romfs is not used; resources are linked in.

### Threads, exit

VitaShell's copy, delete, compress, hash, property and search workers run
as pthreads. On the system's exit request (HOME, close software), SDL
reports `SDL_QUIT`; the controller layer then presses cancel on every
other frame, so viewers and dialogs return to the browser, whose loop
returns from `main` after `SDL_Quit`. Settings > Power > "Exit VitaShell"
does the same.

## Features

| Feature | Status | Notes |
| --- | --- | --- |
| File browser (`sdmc:/`), sorting, marking, scrolling names | ported | folders show 1970 dates: the Switch file system reports no folder times |
| Copy, move, paste, delete, rename, new file, new folder | ported | |
| Properties (size, contents count, dates) | ported | |
| Bookmarks and recent files (`.lnk` links) | ported | recent files are not purged by age (folder age unknown) |
| Text viewer/editor (edit line, search, copy/paste lines, save) | ported | |
| Hex viewer/editor | ported | |
| Image viewer (PNG, JPEG, BMP; zoom, rotate, pan, next/previous) | ported | very large images may exceed the GPU's texture limit |
| Archive browsing and extraction | ported | libarchive 3.7.2 portlib: zip, 7z, rar/rar5, tar, gz, bz2, xz, lz4, zstd, iso, cpio, ...; the password prompt is kept, encrypted archives untested |
| VPK files | changed | opened as zip archives instead of installed |
| Zip compression (name, level) | ported | minizip from upstream |
| SHA-1 of a file | ported | |
| Audio player (MP3 via libmad, Ogg Vorbis via libvorbisfile, lyrics `.lrc`, cover art, speed) | ported | Ogg times fixed; "display off" (triangle) does nothing |
| SFO viewer | ported | reads Vita `param.sfo` files |
| Settings: theme, confirm button, exit | changed | theme changes apply at the next start (upstream had "Restart VitaShell") |
| Themes (colors, images, font) | ported | `font.pgf` replaced by `font.ttf`/`font.otf` |
| Status bar: clock, battery | ported | battery through psm, hidden if unavailable |
| Languages | partly | English only: the system language is not read (the language is fixed to English in `switch2/src/init.c`); `language/english_us.txt` on the SD card can be edited |
| Package (VPK) installation, "Install all", "Install folder" | dropped | Vita application installation (promoter, NpDrm) |
| Refresh LiveArea, refresh license database | dropped | Vita system databases |
| Mount/unmount uma0, imc0, xmc0, USB/game card as ux0 | dropped | Vita devices and kernel plugins |
| USB mass storage (SELECT) | dropped | Vita USB device mode |
| FTP server (SELECT) | ported | tested on 10 November 2026: LIST, RETR, STOR, DELE with curl; a 32 MiB file round trip was identical at 21 MB/s up and 96 MB/s down; Cancel stopped the server; libftpvita built from source (`switch2/third_party/ftpvita`), port 1337, the SD card as `/sdmc:/`; `sceNet*` over BSD sockets and nifm (`switch2/src/psp2_net.c`); the Vita-only `PROM` (install) command is left out |
| QR code scanner and downloads | dropped | Vita camera, sceHttp |
| Ad hoc file transfer | dropped | PSP/Vita ad hoc networking |
| Open decrypted (PFS mounts) | dropped | Vita game data encryption; `pfs.c` state is a stub |
| PSARC archives | dropped | needs the Vita's FIOS library |
| Core dump viewer (`.psp2dmp`) | dropped | Vita core dumps; such files open in the text/hex viewer |
| Media export (music/video/photo apps) | dropped | Vita content apps |
| Network auto-update, updater | dropped | Vita application update |
| Vita kernel/user/patch/usbdevice modules (`modules/`) | dropped | Vita kernel plugins |
| Touch screen | not ported | |
| Power: reboot, power off, standby | dropped | replaced by "Exit VitaShell" |

Menu entries for dropped features that upstream shows greyed when
unavailable (Open decrypted, Ad hoc, Install all/folder, Export) stay
greyed. The home ("home" list) context menu, whose entries are all Vita
mounts, does not open.

## Upstream files

Built unchanged: `context_menu.c`, `hex.c`, `text.c`, `photo.c`,
`audioplayer.c`, `message_dialog.c`, `makezip.c`, `bm.c`, `strnatcmp.c`,
`sha1.c`, `minizip/*`, `audio/{player,id3,mp3player,mp3xing,lrcparse}.c`,
`libmad/*`.

Built with changes (each has a "Modified for Switch 2" note):

| File | Change |
| --- | --- |
| `main.h` | Vita-only includes and the FTP `PROM` hook left out; `VITASHELL_DATA_PATH`; resource sizes from `_end` symbols |
| `main.c` | battery from psm; USB, refresh, install, export, update, QR and ad hoc dialog steps left out (the FTP steps are kept); FTP device `sdmc:` without the slash, no `PROM` command; log files at start |
| `browser.c` | `sdmc:/` roots in `change_to_directory`; VPK as archive; no core dump viewer, QR, USB, ad hoc, app events; SELECT starts FTP; exit on request; zero the last-dir buffer before reading it |
| `main_context.c` | home menu and Vita-only entries hidden |
| `file.c` | the device list is `sdmc:/` |
| `utils.c` | no power ticks, shell locks, app launch, Vita mounts; debug log path |
| `archive.c` | no PSARC or Vita executable checks; error path closes the archive file (was `psarcFileClose`) |
| `io_process.c` | no media export |
| `settings.c` | Switch settings (confirm button, exit) |
| `uncommon_dialog.c/.h` | Switch button symbols; no QR camera view |
| `theme.c`, `language.c/.h`, `resources/english_us.txt`, `vitashell_config.h` | data path, resource sizes, the new settings strings and fields |
| `config.c`, `sfo.c`, `property_dialog.c` | 64-bit fixes (below) |
| `audio/oggplayer.c` | libvorbis time units (below) |

Replaced (not built): `init.c` → `switch2/src/init.c`, `ime_dialog.c` →
`switch2/src/ime_dialog.c`, `audio/vita_audio.c` →
`switch2/src/vita_audio_sdl.c`.

Not built: `package_installer.c`, `refresh.c`, `usb.c`, `qr.c`,
`adhoc_dialog.c`, `netcheck_dialog.c`, `network_update.c`,
`network_download.c`, `pfs.c` (state stubbed in
`switch2/src/dropped_features.c`), `psarc.c`, `rif.c`, `elf.c`,
`sqlite3.c`, `coredump.c`, `modules/`, `updater/`, `release/`, `pkg/`.

### Fixes to upstream code found by the port

- `config.c`: string settings were stored through a `uint32_t *`, keeping
  half of a 64-bit pointer (theme name).
- `sfo.c`, `property_dialog.c`: pointer arithmetic through `uint32_t`
  casts.
- `main.h`, `theme.c`, `language.c`: embedded resource sizes taken from
  the absolute `_size` symbols, which a position-independent executable
  relocates; now `_end - _start`.
- `audio/oggplayer.c`: written for Tremor's millisecond times but calling
  libvorbis, so position and length read zero.
- `archive.c`: an error path closed a libarchive file with
  `psarcFileClose`.
- `browser.c`: the last-directory buffer was used uninitialized when the
  file is missing.
- Character signedness: the text viewer indexes a width table with
  `char`; built with `-funsigned-char` as on ARM.

## Licenses and provenance

The port is GPL-3.0-or-later like VitaShell (`LICENSE`); new files carry
the GPL-3.0 notice ("VitaShell Switch 2 port contributors"). The NRO links
statically:

| Component | Origin | Version | License |
| --- | --- | --- | --- |
| VitaShell | github.com/TheOfficialFloW/VitaShell | `81af709` | GPL-3.0-or-later |
| libmad (vendored by upstream) | Underbit Technologies | 0.15.1 | GPL-2.0-or-later |
| minizip (vendored by upstream) | Gilles Vollant, Mathias Svensson, Nathan Moinvaziri | 1.1 | zlib |
| Noto Sans Regular (bundled font, `switch2/resources/`) | github.com/notofonts/notofonts.github.io, `fonts/NotoSans/hinted/ttf/NotoSans-Regular.ttf` at commit `86ce02c0fe7890f5936c5dffb26fca0797d1be64`, SHA-256 `478c558e…3823` | 2022 | SIL OFL 1.1 (`switch2/resources/NotoSans-OFL.txt`) |
| libftpvita | github.com/xerpi/libftpvita, `77a1d39` (`switch2/third_party/ftpvita`) | 2019 | MIT |
| libounce | Switch 2 devkit (`ounce-devkit` image) | see the image's `ounce-devkit-version.txt` | ISC |
| OunceVK (EGL, OpenGL ES, Vulkan) | Switch 2 devkit (`ounce-devkit` image) | see the image's `ounce-devkit-version.txt` | MIT |
| SDL 2 and portlibs (Switch, in the devkit image) | devkitPro pacman | SDL2_ttf 2.22.0, SDL2_image 2.0.4, FreeType 2.13 (26.2.20), HarfBuzz 10.0.1, libpng 1.6.48, libjpeg-turbo 2.1.2, libwebp 1.0.2, libarchive 3.7.2, zlib 1.3.1, bzip2, xz 5.8.2, lz4 1.9.3, zstd 1.5.7, expat 2.2.5, libvorbis 1.3.7, libogg 1.3.4, Oniguruma 6.9.2 | zlib, FTL, MIT, libpng, IJG/BSD, BSD, BSD-2, zlib, bzip2, 0BSD/PD, BSD, BSD, MIT, BSD, BSD, BSD |

All are compatible with distribution under GPL-3.0. No proprietary SDK or
firmware code is included; the Vita font `resources/*/font.pgf` files in
the upstream tree are not built or installed.

## Known risks

- Startup, browsing, the text viewer, the keyboard, the FTP server and
  Settings > Exit have run on a Switch 2; the other steps below may still
  fail.
- An unexplained crash after `main` returns (during newlib/libnx exit
  after `SDL_Quit`) is being investigated in Chocolate Doom on the same
  stack; VitaShell exits the same way and may hit it.
- Background workers (copy, delete, compress) keep running if the program
  is asked to exit mid-operation; the process exits under them.
- Auto-sleep is not held off during long operations (upstream's power
  lock is a no-op here).
- Listing a folder stats every entry (one or more file system requests per
  file), which may be slow in folders with thousands of files.
- Image size is limited by the GPU's maximum texture size (very large
  photos fail to open with an error).
- The audio player opens SDL audio at the stream's rate and lets SDL
  convert; only one stream plays at a time.

## Not yet tested on hardware

Startup, the `sdmc:/` listing, the clock and battery, folder navigation,
the text viewer, New file with both keyboards, the FTP server and Settings
> Exit worked on 10 November 2026. Not yet tried:

1. Scaling and vsync in detail; the renderer itself works.
2. Fonts: `plInitialize`/`plGetSharedFont` from this program, SDL_ttf on
   the shared font memory, the Nintendo extension button symbols, CJK.
3. Controllers: SDL joystick enumeration across player slots, handheld
   mode, single Joy-Con, button mapping, stick ranges.
4. File system: listing `sdmc:/`, stat of the device root, `statvfs` free
   space, rename/move semantics (EEXIST), timestamps, large file copies,
   paths with non-ASCII names.
5. Writing the default theme/language files to
   `sdmc:/switch/ounceshell/`.
6. Threads: worker threads, their stack sizes, mutexes, the progress
   dialog updates.
7. libarchive formats beyond zip on the Switch build (7z, rar, tar.*),
   encrypted zips.
8. Image decoding (SDL_image portlib), large images, texture limits.
9. Audio output through SDL on `aud:u`: MP3 and Ogg playback, sample-rate
   changes, pause/speed, leaving the player.
10. FTP with a graphical client (FileZilla), active mode (PORT), resumed
    transfers, and exit while a transfer runs.
11. HOME → close software while VitaShell runs.
12. Typing in the software keyboard with a controller (only OK was
    pressed).
