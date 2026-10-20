# AmiSurf

> **Public snapshot (AmiSurf 0.88).** This repository is a clean public snapshot of AmiSurf's development tree, currently
> at version 0.88, published under the GNU General Public License v3 (see `LICENSE`). Beta-tester logs, private notes
> and old build archives were left out, and the earlier commit history (including the `classic` branch and
> `classic-final` tag mentioned below) is not included here. Release downloads are on the AmiStudio site and in
> AmiStudio itself.

AmiSurf (formerly AmiStudio Browser; app of the **AmiStudio** suite, see `docs/RENAME-AMISURF.md`) is a web browser for classic 68k AmigaOS 3.x, built on a **Chrome or Firefox backend**: Google Chrome (default) or
Mozilla Firefox runs on a Linux host and renders the pages; the Amiga (or Linux SDL) program is a thin client that shows the page and sends input back.

> The original from-scratch engine (own HTML parser, layout and painter, NetSurf libcss, QuickJS-ng, MintVID video,
> AmiSSL) was removed from `main`. It is preserved at tag **`classic-final`** and branch **`classic`**
> (`git checkout classic`, then `make classic`).

## How it works

```
Chrome (real browser, own profile)          or   Firefox (own profile, -browser firefox)
   │  CDP over WebSocket, 127.0.0.1:9222             WebDriver BiDi over WebSocket, 127.0.0.1:9322
   │  (screencast PNG frames, input, navigation)     (polled screenshots, input actions, navigation)
asb-chromed  (chrome/host.c, Linux; started and supervised by the AmiStudio Host as its "browser" service)
   │  AmiStudio Host (amistudio-hostd): ASH v1 on TCP 9220, pairing, LAN discovery (docs/HOST.md)
   │  then ASBC protocol on the same socket (chrome/asbc.h; HOSTMODE=LEGACY: plain ASBC on 9223): changed-region tiles as QOI (PNG fallback),
   │  URL/title/loading state, cursor, status, clipboard, find, tabs
AmiSurf (68k MUI, ui/asb_mui_chrome.c + asb_mui.inc; -gadtools: ui/asb_gfx_chrome.c)   or   asb-sdl (Linux SDL2, ui/asb_sdl_chrome.c)
```

## Build

```
make host     # build/host/asb-chromed + build/host/asb-sdl
make amiga    # build/amiga/AmiSurf (MUI, -m68040 -m68881), AmiSurf-020 (MUI, -m68020 -m68881,
              #   FPU check sys/cpucheck.c), AmiSurf-gadtools (Intuition/GadTools fallback, 020+FPU)
make test     # ASBC protocol + QOI round-trip test
make test-host  # host feature integration test (starts its own Chrome; needs an X display)
emu/test68.sh HOSTLOG  # drives a running 68k client in FS-UAE (address bar, reload, find, zoom, tabs, menustrip, About;
              #   GADTOOLS=1, NOCLICK=1 SLOW=4 for the 020 on AGA)
make lha      # dist/AmiSurf-<ver>.lha (dist/mkarchive.sh, jlha c1q)
```

## Run

1. On the PC: install the **AmiStudio Host** (`amistudio-hostd`, repo `amistudio-host`, its `docs/INSTALL.md`; releases
   in hardcopi/AmiStudio-Releases, tag `host-v<ver>`) and mount asb-chromed as its `browser` service:
   ```
   service browser
   proto 2
   exec /path/to/asb-chromed -port {port} -token {token} -idle 0
   handshake asbc2
   ```
   (or `amistudio-hostd --browser /path/to/asb-chromed`). Build asb-chromed with `make chrome-host`.
2. On the Amiga start `AmiSurf [HOST=pc[:port]] [URL]`. Without HOST= it uses the identity folder's `Host` file (0.86: `AmiStudio/Prefs/` or `PROGDIR:Data/`), then
   127.0.0.1:9220 (emulator on the Host PC, no pairing), then a LAN broadcast. A networked Amiga pairs once
   (`amistudio-hostd pair` prints the code; token in `HostToken` next to the `ClientId`, see `ui/asb_store.inc`). Details: `docs/HOST.md`.
3. Linux test client: `build/host/asb-sdl [-host H] [-port P] [URL]` talks plain ASBC to an asb-chromed.

Legacy (0.81 setup, still supported): `build/host/asb-chromed [-browser chrome|firefox] [-port 9223] ...` and
`AmiSurf HOSTMODE=LEGACY [-host H] [-port 9223]`. The CDP and ASBC ports only listen on loopback and have no
authentication; don't expose them. The own Linux host package (`asb-chromed-<ver>-linux-x86_64.tar.gz`,
`dist/host/`) is retired from 0.82: the AmiStudio Host replaces it.

## Features (0.68)

Zoom (25–300 %), large UI, popup menu (☰), right-click menu (open link in new tab, copy link, save image, back,
reload), address-bar suggestions from history and bookmarks, find with match count and next/previous, downloads
list with progress, bookmark/history manager (edit, delete, search), settings, password saving (AES-256-GCM store,
offered after a login, filled in on the next visit), save page as PNG/PDF/HTML, print (Amiga: printer.device),
view source, tab drag to reorder, reopen closed tab, padlock for https, full screen.

Internal pages are `asb://bookmarks`, `asb://history`, `asb://downloads`, `asb://settings` and `asb://passwords`.
asb-chromed serves them on 127.0.0.1:(ASBC port+1). They only accept their own Host header, which blocks DNS
rebinding. Every change needs a per-run key, which blocks CSRF. They cannot be framed.
Typing words in the address bar searches with the engine set in asb://settings.

Host data lives in `$ASB_CONFIG`, or `~/.config/amistudio-browser` (0700) if that is unset. The files are
bookmarks.tsv, history.tsv, downloads.tsv, settings, passwords.dat and key.bin (both 0600). Anyone who can read
key.bin can decrypt the passwords, so keep that directory private.

### Keyboard (Amiga key = A; on Linux use Ctrl)

| Keys | Action |
|---|---|
| A-L | address bar (Up/Down pick a suggestion) |
| A-R / F5, A-. | reload, stop |
| A-[ / A-] (Linux also Alt+Left/Right) | back / forward |
| A-T, A-W, A-Shift-T | new tab, close tab, reopen closed tab |
| A-Shift-[ / A-Shift-] | move tab left/right (or drag the tab) |
| A-1..8 | go to tab N (Linux: also Ctrl+Tab / Ctrl+PgUp/PgDn) |
| A-F, A-G / A-Shift-G (F3) | find, next / previous match |
| A-+ / A-- / A-0 (Linux: also Ctrl+wheel) | zoom in / out / 100 % |
| A-D, A-O, A-Y (Linux Ctrl+H), A-J | bookmark page, bookmarks, history, downloads |
| A-S, A-P, A-U | save as PNG, save as PDF, view source |
| A-, | settings window (Amiga) |
| F10 (Amiga), F11 (Linux) | full screen |
| Help | About (Amiga) |
| A-Q | quit |

(Left-Amiga+M/N are Intuition's screen keys, so the menu has no shortcut. Click the ☰ icon.)

### Amiga tooltypes

| Tooltype | Meaning |
|---|---|
| `CHROMEHOST=host` `PORT=9223` `TOKEN=…` | where asb-chromed runs (TOKEN only for -listen hosts) |
| `URL=` / `START=`, `HOME=` | start page, home page |
| `THEME=PRO` | dark theme (default light) |
| `SCREENFONT=TOPAZ` | Topaz instead of the screen font |
| `UIFONT=helvetica/15` | UI font and size (any installed .font) |
| `TOOLBAR=LARGE` or `BIGUI` | 40 px buttons, taller tab/toolbar/status bar |
| `BROWSER=CHROME`/`FIREFOX` | backend the host should use (also Settings > Browser); unset = the host's `-browser` |
| `SCREENMODE=AUTO`/`WB`/`0x…`/`1280x800`, `MODEREQ` | screen choice: best RTG mode, Workbench window, a fixed mode ID, a size (exact or closest RTG mode), or the ASL requester |
| `SPLASHHOLD=ms`, `DEBUG` | splash time, debug output |
| `STATS` | speed numbers in the status bar (also in the menu) |
| `FRAMES=QOI` | use the old QOI/PNG frames instead of pre-converted ones |

The settings window (A-,) saves these settings to PROGDIR:Data/AmiSurf.prefs (0.86; nothing in ENV:/ENVARC:).

SDL client options: `-uisize N`, `-large`, `-uifont file.ttf`, `-full`. `ASB_UISIZE=N` does the same as `-uisize`;
`ASB_BROWSER=firefox|chrome` picks the backend.

## Speed (0.69)

The client announces its pixel format, so frames arrive already in the screen's format. It gets RGB565/555 or
24/32-bit on RTG, and palette indices that the host dithered for AGA. Frames are raw or RLE-packed, so the client
only copies them; QOI/PNG stay as the fallback (`FRAMES=QOI`). The client acknowledges every update and the host
never has more than 2 in flight, so a slow Amiga skips frames instead of falling behind. Scrolling becomes a
"move rows by dy" command plus the newly revealed strip. `STATS` (tooltype, or the menu) shows fps, KB/s, client
ms per frame and the host's encode ms in the status bar. Numbers are in `bench/`.

## Backends (0.71): Chrome or Firefox

`asb-chromed -browser chrome` (default) drives Chrome over the Chrome DevTools Protocol. `-browser firefox` drives
Firefox over **WebDriver BiDi**, the W3C protocol Firefox serves on `--remote-debugging-port` (Firefox's CDP support
is deprecated and not used). The host looks for Firefox in `/opt/firefox`, `/usr/lib/firefox`, `/usr/bin/firefox(-esr)`
or `$ASB_FIREFOX`. Firefox gets its own profile, `<profile>-firefox`, and the host writes a `user.js` in it (download
folder, no first-run pages). A client can ask for a backend: `BROWSER=` tooltype, Settings > Browser, or
`ASB_BROWSER=firefox` for asb-sdl. The choice is sent in HELLO (an optional 5th byte, so old hosts and clients still
work), and the host stops its browser and starts the other one. Only one browser runs at a time.

Same on both: navigation, back/forward/reload/stop, mouse, wheel, keyboard and text input, tabs (new, close, switch,
reorder, reopen), find with count, zoom, copy/paste, context menu info, save as PNG/HTML/PDF, view source, bookmarks,
history, internal `asb://` pages, password saving and filling, downloads (to AmiKit:Downloads), cookies and logins
(kept in each browser's own profile).

| Gap | Chrome (CDP) | Firefox (BiDi) |
|---|---|---|
| Frames | pushed by the screencast, only when the page changes | polled with `captureScreenshot` (~25/s while a client is connected, one in flight); idle pages cost host CPU |
| Download progress | received/total bytes while downloading | start and end only (BiDi has no progress event); the size is read from the file at the end |
| Right mouse button | sent to the page, then the AmiStudio context menu | not sent (a native Firefox menu would open on the hidden display); `contextmenu` handlers on pages don't run |
| Automation flag | `navigator.webdriver` hidden | Firefox marks a BiDi session as automated (striped address bar on the host display); some sites (Google) show a CAPTCHA more often |
| Tab titles | for every tab | for tabs that have been active (BiDi's tree has no titles); others show their URL |
| JavaScript dialogs | shown on the host display | left open (`unhandledPromptBehavior: ignore`); the page waits until it is closed |
| Print to PDF | Chrome's `printToPDF` | `browsingContext.print` (Firefox page setup) |
| Popups / `target=_blank` | listed as tabs | listed as tabs |
| Start-up | ~2 s | ~4 s, plus ~4 s to switch backend |
| YouTube video | plays (Big Buck Bunny: 27 fps Linux, 14 fps 040 RTG, bench/video-071.txt) | YouTube stops playback with "Something went wrong" under a BiDi session (plain Firefox plays it) |

## Remote host, legacy mode (HOSTMODE=LEGACY; with the AmiStudio Host use pairing instead)

asb-chromed listens on loopback only. The safest setup is to keep it that way and tunnel:

* **SSH:** run `ssh -N -L 9223:127.0.0.1:9223 user@odin` on a machine the Amiga can reach (or on the Amiga, with an
  SSH client), then point CHROMEHOST at that tunnel end.
* **WireGuard:** run `asb-chromed -listen 10.8.0.1 -token <long random>` on the VPN address only.

`-listen ADDR` without `-token` (8+ characters) is refused. The token is sent in the clear, because ASBC has no TLS
(68k AmiSSL would cost too much CPU per frame), so only use it on a trusted LAN or VPN. After 5 failed handshakes in
60 s, a remote IP is banned for 5 minutes. Never forward the port from the internet.

See `DEVELOPING.md` for layout, emulator and rules, and `dist/docs/CHANGES` for history.
