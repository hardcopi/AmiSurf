# Developing AmiSurf

A web browser for classic 68k AmigaOS 3.x using a Chrome backend: `asb-chromed` drives an installed Google Chrome on a
Linux host over the DevTools protocol (CDP) and streams the page to thin clients over our ASBC protocol. Clients:
`AmiSurf` (68k Intuition) and `asb-sdl` (Linux SDL2). `ui/asb_mui_chrome.c` (MUI client) is in progress and
not built yet.

The old from-scratch engine (HTML/CSS/layout/paint, libcss, QuickJS-ng, MintVID, AmiSSL) lives at tag `classic-final`
and branch `classic`.

## Setup

    scripts/setup.sh            # apt packages, bebbo m68k-amigaos-gcc in /opt/amiga, build, tests
    scripts/setup.sh --no-toolchain --no-emu   # host-only

Cross compiler: https://github.com/bebbo/amiga-gcc installed to `/opt/amiga` (`AMIGACC` in the Makefile).
The host also needs Google Chrome (`google-chrome-stable`/`google-chrome`) or Chromium.

## Build

| command | output |
|---|---|
| `make host` | `build/host/asb-chromed` (Chrome host), `build/host/asb-sdl` (Linux client) |
| `make amiga` | `build/amiga/AmiSurf` (68020, soft-float; the only 68k binary, runs on any 020+) |
| `make test` | `build/host/asbc_test`: ASBC handshake + message round trip, QOI encode/decode round trip |
| `make lha` | `dist/AmiSurf.lha` via `tools/package.sh` (needs an LHA archiver that can create archives, e.g. jlha) |

## Tests

* `make test` must stay green. Add cases to `test/asbc_test.c` when the protocol changes.
* Visual check on Linux: start `asb-chromed` on an X display, then `asb-sdl -shot out.bmp -quit 8000 URL`.
  `tools/linshot.sh` wraps this on Xvfb `:78`.

## Emulator (bring your own Amiga)

Nothing copyrighted is in the repo: no Kickstart ROMs, no Workbench/AmigaOS files, no MUI, no emulator binaries, no
disk or HD images (`.gitignore` blocks them). To test on an emulated Amiga:

1. Install FS-UAE (or Amiberry) and get a legal Kickstart 3.x ROM and AmigaOS 3.x (e.g. Amiga Forever, Hyperion OS 3.2).
2. Install AmigaOS to a directory used as a UAE hard drive (default `emu/hd/System`, ignored by git).
   Start `asb-chromed` on the host first; the client connects to 127.0.0.1:9223 through bsdsocket host networking.
   In `S:User-Startup` (or the end of `S:Startup-Sequence`) add `Execute S:asp-launch`: `run68.sh` writes the commands
   to run into that file.
3. Point the templates at your files and run:

       export ASB_KICKSTART=/path/kick31-a1200.rom ASB_DISKS=/path/adfs ASB_HD=/path/System
       cp build/amiga/AmiSurf* $ASB_HD/ASB/
       emu/run68.sh "cd SYS:ASB
       AmiSurf https://news.ycombinator.com/" 90
       emu/shots.sh /tmp/hn "cd SYS:ASB
       AmiSurf https://news.ycombinator.com/" 75 25 45   # screenshots at 25 s and 45 s

   Templates: `emu/asb.fs-uae.in` (A1200, JIT 68040, 128 MB Z3, UAEGFX RTG, host bsdsocket networking),
   `asb020.fs-uae.in` (real-speed 68020, no JIT), `asblow.fs-uae.in` (low memory). The rendered config goes to
   `emu/local/`. The emulator runs headless on its own Xvfb display (`ASB_DISPLAY`, default `:77`).
   For Amiberry, copy the settings from the template into an Amiberry `.uae` config (same CPU/RTG/bsdsocket options).

## Module layout

| path | role |
|---|---|
| `chrome/host.c` | `asb-chromed`: starts Chrome (own profile `~/.config/amistudio-chrome`), CDP WebSocket client, screencast → frame diff → QOI dirty tiles (PNG fallback), input/navigation/tabs/find/clipboard/downloads |
| `chrome/asbc.c`, `asbc.h` | ASBC wire protocol (handshake, length-prefixed messages, op codes) shared by host and clients |
| `chrome/qoi.h` | vendored QOI codec (MIT) |
| `ui/asb_gfx_chrome.c` | 68k Intuition client (address bar, bookmarks/history, find bar, tabs, downloads to AmiKit:Downloads) |
| `ui/asb_sdl_chrome.c` | Linux SDL2 client |
| `ui/asb_mui_chrome.c`, `ui/icons/` | MUI client + toolbar icons (in progress, not built) |
| `ui/muiinc/`, `sys/rtg_amiga.h`, `tools/sfdc_vararg.h` | Amiga headers |
| `third_party/stb/stb_image.h` | PNG decoding of screencast frames |
| `test/asbc_test.c` | protocol/QOI test |
| `dist/` | release archive + docs (README, CHANGES, ATTRIBUTIONS) and `info/` icons; `tools/package.sh`, `tools/licenses.sh` |
| `emu/` | FS-UAE templates and scripts (bring your own ROM/OS) |
| `shots/` | reference screenshots per version |

## Roadmap

Priorities: **YouTube** and **Gmail** usable from the Amiga through the Chrome host; get the MUI client built; cut frame
latency and bandwidth on slow 68k machines. Versioning: small steps (0.66, 0.67, ...). **1.0 is reserved for a
full-featured browser.**

## Rules

* Main target is 68020 without FPU (`-m68020 -msoft-float`); there is one binary for all CPUs. Keep the client free of floating point so it stays fast on 040/060 (no FPU emulation traps). Keep it working on AmigaOS 3.1+.
* Never commit Kickstart ROMs, AmigaOS/Workbench files, MUI, emulator binaries, disk images, cookies, Chrome profiles or secrets.
* Third-party code must be permissively licensed and listed in `dist/docs/ATTRIBUTIONS` with its licence in `tools/licenses.sh`.
* Every release: bump `ASB_VERSION` in `ui/asb_gfx_chrome.c`, `ui/asb_sdl_chrome.c`, `ui/asb_mui_chrome.c` (and `$VER`),
  update `dist/docs/CHANGES` and `README`, run `make test`, take Linux and Amiga shots, run `make lha`.
* Commit in small checkpoints; keep `make test` green.
