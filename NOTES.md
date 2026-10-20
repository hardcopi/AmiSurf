> Historical notes from the classic engine (now at tag `classic-final`). Toolchain notes still apply.

# Setup notes (2026-10-14)

## Toolchain
- `/opt/amiga/bin/m68k-amigaos-gcc` (bebbo 6.5.0b), NDK under `/opt/amiga`
- Flags match Petscii: `-m68040 -mhard-float -noixemul`

## Emulator
- Source: `/workspace/amiga/a1200.fs-uae` + `/workspace/amiga/hd/System`
- Copy: `emu/asb.fs-uae` + `emu/hd/System` (17M after excluding PhotoCraft/AmiStudioPhoto scratch)
- Petscii runner pattern: `amiga-photocraft-c/pcconvert/tests/run68.sh` (S:asp-launch + o.done)
- Our runner: `emu/run68.sh` — same idea, scoped to our HD; kill only `fs-uae asb.fs-uae`

## Networking
- Enabled `bsdsocket_library = 1` in FS-UAE. Log shows UAE installs `bsdsocket.library 4.1`.
- No Roadshow/AmiTCP on the HD; host-socket emulation is enough for development.
- Verified in emu: HTTP GET to host `127.0.0.1:8089` (redirect + chunked) works; offline `-f` layout matches host.
- Blockers: (1) HTTPS not implemented (AmiSSL / tls module next). (2) Box egress blocks raw outbound TCP/80 (`198.18.0.1` times out); HTTPS via system curl works through SAND proxy — native Amiga sockets won't see that proxy.

## 2026-10-14 tls + MUI
- Host HTTPS via OpenSSL, direct TCP/443 (no proxy needed). example.com, info.cern.ch, wikipedia.org redirect OK; expired.badssl.com rejected.
- Amiga: no AmiSSL anywhere on box/HD; stub error. HTTP via UAE bsdsocket OK.
- MUI asb-mui: muimaster v21.227, window open=1, page loaded into TextObject, auto-quit. Headers in ui/muiinc (copied from Petscii).
- No screenshot tool (import/xwd/convert) on box.

## 2026-10-14 AmiSSL + MUI listview
- AmiSSL 5.27 (GitHub jens-maus/amissl) OS3 + SDK in third_party/amissl (lha archives + extracted).
- Installed into OUR emu HD only: LIBS:amisslmaster.library, LIBS:AmiSSL/amissl_v362.library (68020-40),
  SYS:AmiSSL/{Libs/AmiSSL/amissl_v362.library,Certs,OpenSSL}, User-Startup `Assign AmiSSL: SYS:AmiSSL`.
  First attempt with lib only under AmiSSL:Libs/AmiSSL -> OpenAmiSSLTags=2; after adding LIBS:AmiSSL copy it works.
- tls/ AmiSSL backend: OpenAmiSSLTags (SocketBase, errno), SSL_CTX verify peer via AmiSSL:Certs, SNI + hostname check.
- Emu HTTPS proven: https://example.com and https://info.cern.ch via UAE bsdsocket -> host :443. CERN HTTPS output byte-identical to host.
- asb-mui: Listview (string list), double-click row -> last [n] link, Back history (32), headless -click N / -back.

## 2026-10-14 Phase 3 (CSS/layout/fonts)
- css/: own parser, fixed-point lengths (68k libnix had no atof without libm).
- Layout emits FS_* control bytes; font_render maps to markup/MUI/ANSI. Goldens regenerated (markup mode).
- Quirks: tables reset text-align (HN <center>); td/th are inline cells in non-simple (layout) tables; simple tables (<=60ch cells, no nesting) render as aligned columns with bold header + rule.
- Emu: all 6 offline fixtures byte-identical amiga==host; HTTPS CERN identical; MUI click/back/fwd + urlbar sync proven.
- Known: link marker [n] after block-level link content lands on its own line (Wikipedia TOC); color parsed but only used as MUI link pen; font_pick not yet used for per-line Amiga fonts (Listview is single fixed font).

## 2026-10-14 Phase 5a forms + cookies
- store/cookies: Set-Cookie (Domain/Path/Max-Age/Expires/Secure/HttpOnly), host-only, path-match, deletion, persistence (tab file). Session cookies not persisted.
- html/forms: text/password/hidden/checkbox/radio/submit/reset/button, select(+optgroup), textarea; GET (replaces query) and POST urlencoded; CRLF in textarea.
- net: http_request(method,…); POST, 301/302/303 -> GET, 307/308 keep; Cookie header + Set-Cookie capture via net_set_jar().
- Layout shows controls with {fN}; Fields: section in dumps.
- CLI: -jar -set -check -uncheck -submit [name]. MUI: double-click {fN} row -> edit gadget (secret for password) + Set; checkbox/radio toggle; submit button submits. Jar default ENVARC:AmiSurf.cookies.
- Emu: forms layout amiga==host; GET search, POST login->302->secret with cookie, cookie reused from jar file, MUI login via GUI path; jar persisted to ENVARC.
- Gap: Amiga clock in FS-UAE is ~9h behind host (expiry timestamps differ); fine for Max-Age, wrong for absolute Expires vs real time until RTC/TZ set.

## google.com test (2026-10-14)
- Homepage over AmiSSL in emu: OK. HTTP 200, ~85 KB ISO-8859-1, no redirect, no consent page (US egress), 3 cookies (NID, AEC, __Secure-STRP) stored. Cert chain (GTS) verified with AmiSSL Certs.
- Search form (q=amiga, btnG) submits GET /search; Google now answers every no-JS client with a "enablejs" noscript page (Lynx/w3m/Links/MSIE6 UAs get "Your browser isn't supported anymore"). No results page is possible without JS — UA tweaks don't help.
- Fixes: 1) Amiga stack: Shell default (64K here) ignored our __stack; deep DOMs (~5 KB/walk frame) overflowed -> hang/guru 80000004. sys/bigstack.c StackSwaps to a 1 MB stack for asb and asb-mui. 2) tables containing form controls/links/images no longer squashed into text columns (google's search box vanished). 3) <noscript> is rendered (we have no JS); display:block now overrides an earlier display:none. 4) CSS rules array grows geometrically. 5) Accept-Language: en.
- Tracing: `touch SYS:ASB/trace.on` (Amiga) or ASBTRACE=file (host) -> net/layout milestones in SYS:ASB/tr.txt.

## DuckDuckGo HTML test (2026-10-14)
- html.duckduckgo.com/html/ over AmiSSL: home GET 3.1 KB, search POST q=amiga -> ~28 KB, 10 results. Result hrefs are direct (no /l/?uddg= redirect wrapper in this version); clicking [1] loads en.wikipedia.org/wiki/Amiga (~696 KB, 2214 lines, 965 links) fine.
- Fixes: duplicate hrefs share one link number (DDG title/url/snippet all link the same URL); <select> shows the selected option's label; field editor resets on navigation; asb-mui -thenclick N clicks a link after -submit.
- Known: state_hidden text input shows (hidden by external CSS, which we don't fetch); h2 dash underlines are heavy for result titles.

## External CSS + 1920x1080 emu (2026-10-14)
- cache/cssload.c: <link rel=stylesheet> fetched via net/ (redirects, HTTPS), media screen/all (print/alternate skipped), 512 KB/sheet, 8 sheets/page, 24-URL session cache, shallow @import (4). css/: descendant + child combinators, @media screen/all parsed, rule index by id/class/tag, visibility:hidden/opacity:0 treated as not rendered.
- net_init idempotent; asb-mui keeps bsdsocket open for the session (CSS fetches during layout).
- Emu: tools/setmode writes screenmode.prefs -> UAE 1920x1080 16-bit RTG; Xvfb :77 1920x1080; asb-mui window 96%x92% of screen, wrap width from list width / font (re-laid out on resize).

## Linux-first graphical front end (2026-10-14)
- Direction: develop on Linux first, port later. paint/paint.{h,c}: portable painter over a 5-call backend (measure, line_h, text, fill, frame) — parses layout lines (FS_* style bytes, new FS_H1..6 heading marker, [field:]/[select:]/[Submit:]/[x] check {fN}, [N] link marks) into styled runs, form widgets, link hit boxes. Amiga port = RastPort backend (Text/TextLength/RectFill).
- ui/asb_sdl.c (make sdl): SDL2 + SDL_ttf (Liberation Sans), 1920x1080, wrap cols from font metrics, click links/buttons/checkboxes, wheel/PgUp/PgDn, Backspace = back, -set/-submit/-thenclick/-shot/-quit for headless. tools/linshot.sh grabs Xvfb :78 with ffmpeg.
- Layout now marks link spans (FS_L) — MUI also gets link pens. html_set_utf8(1) on Linux: entities -> UTF-8, Latin-1 pages transcoded, 0xA0 not split inside UTF-8.

## Pixel layout, typing, images (2026-10-14)
- Pixel mode: layout_set_measure(m, spacew, indent_mul, line_h) — layout wraps by px using paint_measure() (same code path as painting, so widgets/link marks/images measure exactly). Tables pad cells to pixel column edges (±1 space). Text mode (asb, asb-mui) unchanged; goldens identical.
- asb-sdl typing: click URL bar (or Ctrl-L) / click a text field to focus; type, Backspace, Enter navigates / submits the field's form; Esc unfocuses. Headless: -typeurl URL, -type NAME=TEXT, -typeenter NAME=TEXT.
- image/: stb_image (PNG/JPEG/GIF first frame; compiles for m68k) + own ICO (PNG-in-ICO, 1/4/8/24/32-bit DIB). Limits 4096x4096, 4 MB compressed. cache/imgcache: session URL->RGBA cache (512 entries, 128 MB).
- Layout reserves boxes: CSS width/height > attrs > intrinsic (when decoded) > alt-text placeholder; scaled to fit width; <=2x2 trackers skipped. Progressive: page paints first, images fetched one per idle tick, relayout when an image's true aspect differs from its placeholder.
- Tests: decoder fixtures test/pages/img/t.{png,jpg,gif,ico}, pixel-wrap + img box unit tests (94 total); make test-sdl renders test/pages/img/page.html headless.

## v5 Amiga side (2026-10-14)
- asb_gfx.c + MUI: video player (vidui.inc) in the page area; About via Project > About menu, toolbar About, Help key, about:. Player wakes the UI with a signal bit (MUI signal input handler / Intuition Wait). Frames: RTG truecolour WritePixelArray, AGA ordered dither onto the 4x4x4 pen cube. 020 build defaults to Keyframes+audio, 040 build (-DASB_040) to Direct. Paula audio via MintVID.
- Emu fps (480x270 H.264/AAC, 12 s, served from host :8091): JIT 040 RTG Direct 22.7-23.7 fps, 0 dropped; Keyframes 0.9-1.0 fps; real-speed 14 MHz 020 AGA Keyframes ~0.2 fps (audio/clock also lags; exit after -auto was slow -> TIMEOUT).
- test/vidunit.c (make test-video, also build/amiga/vidunit): ASV self-test, <video>/<iframe> rewrite, URL detection: 16/16 host and in emu.
