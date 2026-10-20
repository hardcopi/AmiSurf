# AmiSurf and the AmiStudio Host (0.82)

AmiSurf no longer starts or talks to its own engine. The **AmiStudio Host** (`amistudio-hostd`, repo
`amistudio-host`, `docs/PROTOCOL.md` there) runs on the PC, starts `asb-chromed` on demand as its `browser`
service and passes AmiSurf's connection through. Client code: `ui/asb_ash.inc`.

## Connection
1. Target (`target_pick`): `HOST=`/Settings Host/`-host`,`-port` (`host[:port]`, default port 9220) →
   the identity folder's `Host` file (0.86, below) → `127.0.0.1:9220` → LAN search: UDP `"ASH?"` to 255.255.255.255:9220,
   first `"ASH!"` answer (`port=`, `name=`) wins, 1.5 s, polled from the main loop. A found Host is used until a
   connect to it fails. 0.81 prefs that saved `127.0.0.1:9223` (the old default) count as "automatic"; port 9223
   is ignored in Host mode (it is the old engine's / the Host's compat port).
2. Connect: 0.81's bounded connect (3 s, timer break) and offline state machine with backoff (2/4/8/16/30 s).
3. Handshake (4 s): `ASH1` u16 1, service `browser` (`SERVICE=`), version 2 (ASBC_VERSION), token, client id
   `AmiSurf/0.82 68040`. Reply `ASH1` u16 · status · u16 version · message. On status 0 the socket is ASBC:
   AmiSurf sends HELLO at once (no ASBC handshake: the Host did the backend's).
4. Token: `TOKEN=` if set, else `HostToken` in the identity folder (0.86, below), hex only — the launcher's file
   (hardcopi/AmiStudio-Launcher branch host-client, `src/hostc.c`). Loopback needs none.

## Statuses (requester once per kind, then status bar + retries)
| status | AmiSurf |
|---|---|
| 1 bad/missing token | "not paired … Pair now?" → **Pair with Host** dialog (code from `amistudio-hostd pair`) → service `pair` v1, `PAIR_REQ "code\nAmiSurf/0.82 680x0"` → token written to ENVARC: and ENV:AmiStudio/HostToken, reconnect. Wrong code: "Pairing failed: Wrong code (N tries left)", Try again/Cancel |
| 2 no such service | "the Host has no 'browser' service (add AmiSurf's engine to host.conf)" |
| 3 too old | "AmiSurf is too old for this Host (needs protocol N+): update AmiSurf" |
| 4 too new | "the Host is older than AmiSurf (protocol up to N): update the Host" |
| 5 busy | "the Host's browser is busy (another Amiga …)", keeps trying |
| 6 refused | "refused by the AmiStudio Host: <message>", keeps trying |
| not ASH | "is not an AmiStudio Host (an old asb-chromed? then set HOSTMODE=LEGACY)" |
| nothing found | "No AmiStudio Host found (127.0.0.1:9220, ENV:AmiStudio/Host, LAN search) …" |

## HOSTMODE=LEGACY
Exactly 0.81: plain ASBC (`ASBC` u16 2 token) to `CHROMEHOST`/127.0.0.1 : `PORT`/9223; AUTOHOST/HOSTCMD start the
AmiKit edition's `Host/run-host.sh --background --legacy --port 9223 --idle 30` (plain asb-chromed, no Host).

## Pairing from the menus (0.83)
- MUI menu Project: **Pair with Host...** (`pair_now`: the pairing dialog for the current target, any time, also
  while connected; success replaces the token in ENV:/ENVARC:AmiStudio/HostToken and reconnects) and
  **Forget Host token...** (confirm, delete both files, reconnect: a LAN Host then says "not paired").
- ≡ menu (MUI and GadTools): **AmiStudio Host (pair) ...** → requester with the target (`host:port` and where it
  came from), connected or not, the token file (or "TOKEN= overrides"), and *Pair... / Pair again... / Forget token /
  Cancel*. One item, so the ≡ list stays 29 rows.
- HOSTMODE=LEGACY: both say there is nothing to pair. If `TOKEN=` is set it still wins over the paired token; the
  status bar says so after pairing.
- Re-pairing: Host 0.1.1 replaces the old token (same client id). Forget only deletes the Amiga's copy; the Host
  keeps that entry until `amistudio-hostd revoke ID` (no revoke over ASH v1).

## AmiKit edition (0.83): the bundled Host
`make amikit-bundle` downloads the official `amistudio-host-0.1.3-linux-x86_64.tar.gz` (host-v0.1.3 in
AmiStudio-Releases, SHA-256 f9d36ce9…05c6c checked; 0.1.2 before, same engine files byte for byte) and, with the default `AMIKIT_CHROMIUM=host`, puts the whole release
folder unchanged into `AmiSurf/Host/amistudio-host/` (amistudio-hostd, ash-cli, `engine/engine.sh`, `engine/asb-chromed`
built from bd10794, `engine/chromium` = ungoogled-chromium 155.0.8059.79-1): one Chromium in the archive. Our
`Host/engine.sh` runs `amistudio-host/engine/engine.sh` with `ASB_STATE=Host/data`, so the edition keeps its data folder.
`make amikit-bundle AMIKIT_CHROMIUM=own` flips back to the edition's own copy (only `amistudio-hostd` from the tarball,
plus `Host/asb-chromed` from build/static and `Host/chromium` from build/uc). `asb-state.sh` picks whichever layout is
there (`HOSTD`, `ENG`, `CHROME`), so the Host/ scripts are the same for both. Sizes (0.83, Host 0.1.3): host 248,719,479 bytes,
own 248,685,057 bytes (the same Chromium and a byte-identical asb-chromed; +34 KB for the Host's docs, ash-cli, licences). Shipping the whole Host
tarball next to our own copy would have been ~480 MB. Next to this: `engine.sh`, `run-host.sh`, AmiSurf (Host mode, target 127.0.0.1,
nothing listening, `C:host-run` present) runs `C:host-run sh "<drawer>/Host/run-host.sh" --background --port 9220 --idle 30` (`--background`: the
standalone Host 0.1.2 run-host.sh only detaches with it; ours detaches anyway and accepts it).
run-host.sh restores the exec bits, writes `Host/data/hostd/host.conf` (`lan no`, `discovery no`, `update_check no`,
`bundled_browser no`, `service browser` → `sh engine.sh -port {port} -token {token} -cdp {port2} -idle 30`) and starts
`amistudio-hostd run --confdir Host/data/hostd --listen 127.0.0.1 --no-discovery --no-update-check --exit-idle 30`,
detached (setsid). The engine quits after 30 min without a client (`-idle`); the Host itself after 30 min in which no
client connection was open or opened (`--exit-idle`, Host 0.1.3; it replaced 0.83's earlier idle-watcher loop).
Before starting, AUTOHOST connects once more after 1 s, and run-host.sh asks `amistudio-hostd status 127.0.0.1:9220`
(Host 0.1.3: 2 s timeout built in, so our `timeout 6` wrapper is gone): if a Host answers (e.g. one the launcher 0.1.4 started via Rabbit Hole) nothing is started; another
program on the port is reported. Pairing sends PAIR_REQ `code\nAmiSurf/<ver> <cpu>\n<ClientId>` (ENVARC:AmiStudio/ClientId,
`amiga-` + 16 hex, made once) so Host 0.1.1 replaces this Amiga's previous token.
Its config is separate from a system-wide Host (`~/.config/amistudio-host`); if that one already has 9220,
run-host.sh says "already running" and AmiSurf uses it. `--lan` serves real Amigas too (`sh run-host.sh --pair`).
If the Host answers but refuses (e.g. a system Host without a browser service) AUTOHOST does not run.
`Host/move-to-host.sh` copies `Host/data/{config,profile}` to the standalone Host 0.1.2's
`~/.local/share/amistudio-host/browser` (or `$ASB_STATE`), only into empty/missing folders, never overwriting,
refusing while a profile is locked; and `Host/data/hostd/tokens` to `~/.config/amistudio-host/tokens`
(`$AMISTUDIO_HOST_DIR`) only if no tokens file exists, so paired Amigas stay paired. host.conf and logs are not copied.
0.1.2's layout matches (its engine.sh: `$ASB_STATE/config`, `$ASB_STATE/profile`; confirmed by the AmiStudio agent).

(Done in 0.83 with Host 0.1.3: the idle-watcher loop is gone, run-host.sh passes `--exit-idle $IDLE` (minutes).)

## Tested (FS-UAE, KS 3.1, MUI 21.227, amistudio-hostd 0.1 on the box, 2026-10-17)
- 040 MUI, loopback, no HOST=: 127.0.0.1:9220 → Host starts asb-chromed → page (`shots/host-loopback-082-emu.png`).
- HOST=172.30.0.2 (box LAN address, so the Host treats the emulator as a LAN client): status 1 requester
  (`shots/host-not-paired-requester-082-emu.png`) → Pair dialog (`shots/host-pair-dialog-082-emu.png`) → wrong
  code → "Wrong code (3 tries left)" (`shots/host-pair-wrong-code-082-emu.png`) → Try again → right code → token
  saved in ENVARC:AmiStudio/HostToken, `amistudio-hostd tokens` lists "AmiSurf/0.82 68040", reconnect accepted.
- Statuses 2 (SERVICE=nosuch on the real Host), 3/4/5/6 (fake Host): requesters with the texts above.
- No Host running: localhost refused → LAN search → "No AmiStudio Host found" requester (`shots/host-none-found-082-emu.png`).
- Host listening on 172.30.0.2 only: localhost refused → LAN search finds 'boxhost' at 172.30.0.2:9220 → connects
  with the saved token.
- ENVARC:AmiStudio/Host = 172.30.0.2:9220 → used directly.
- HOSTMODE=LEGACY -port 9611 → plain asb-chromed works; Host mode pointed at that asb-chromed → "not an AmiStudio Host".
- AmiSurf-020 (68020+68882, AGA 640x256) and AmiSurf-gadtools via the Host on loopback.

## Tested 0.83 (FS-UAE, amistudio-hostd 0.1 --lan, HOST=172.30.0.2 so the emulator counts as a LAN client)
- No token → "not paired" requester → Later; ≡ > AmiStudio Host (pair) ... (`shots/host-menu-unpaired-083-emu.png`)
  → Pair... → wrong code 111111 → "Wrong code (4 tries left)" → Try again → Cancel (still offline, no requester loop).
- MUI menu Project > Pair with Host... (`shots/menu-pair-forget-083-emu.png`) → right code → paired, connected, page.
- Connected: ≡ > AmiStudio Host → "Pair again... | Forget token | Cancel" (`shots/host-menu-repair-083-emu.png`) →
  new code → token file replaced, reconnected with the new token (Host log: token b1fb71b3).
- Project > Forget Host token... → confirm → both files deleted → reconnect →
  "not paired" requester.
- AmiSurf-gadtools: ≡ list with the item (`shots/gadtools-menu-host-083-emu.png`) → Pair... → right code → page.
- AmiKit LHA unpacked with jlha (exec bits lost) onto the System: directory drive, test/rh host-run stand-in:
  AUTOHOST → run-host.sh restored the bits, Host up, engine started, page with sound (stats "snd P8 24/s", "av:" lines,
  `shots/amikit-host-audio-083-emu.png`), https page (`shots/amikit-host-wikipedia-083-emu.png`). Killing the Host
  stopped the engine. `--idle 1`: Host stopped after 1 min without any Amiga, and after the engine idled out.

## Tested with Host 0.1.1 (official release binary, 2026-10-17)
- Reaper: engine `-idle 1` exits → Host logs "backend exited status 0 … starts again for the next client", no zombie;
  so run-host.sh's zombie workaround is gone (the watcher just checks `pgrep -P`).
- run-host.sh with a Host already on 127.0.0.1:9220 → "an AmiStudio Host already answers … not starting another"
  (exit 0, one Host); with a non-Host listener on 9220 → "taken by a program that is not an AmiStudio Host" (exit 1).
- FS-UAE, AmiKit LHA, no Host: AUTOHOST `--background` → Host 0.1.1 → engine → page with sound. With a Host already
  running (started from another folder, like the launcher would): AmiSurf connects to it, no host-run issued.
- Re-pair (HOST=172.30.0.2, Host --lan): `tokens` shows one entry with `[client id amiga-…]`; the Host logs
  "Paired as f06cc35f (replaces b49bcb59)"; the Amiga's token file changed and the reconnect used the new token.
- move-to-host.sh: copies into empty targets, keeps non-empty ones, refuses while the engine runs, accepts a stale lock.

## Tested with Host 0.1.2 (official release, 2026-10-17)
- LHA (lha x, exec bits lost) into a fresh "My Drive": the autostart command
  `sh ".../Host/run-host.sh" --background --port 9220 --idle 30` restored the bits, started
  `Host/amistudio-host/amistudio-hostd` (log: "AmiStudio Host 0.1.2 ... engine: host"); FS-UAE AmiSurf on 127.0.0.1:9220
  → Host started `amistudio-host/engine/asb-chromed` + its Chromium with `Host/data` → audio test page rendered and
  played (asb-chromed "audio capture parec", AmiSurf "av:" lines, 0 underruns). Running it again: "already answers".
  Killing the Host by PID stopped the engine.
- Standalone 0.1.2 (`run-host.sh --background`, own confdir/ASB_STATE) on 9220, then the AmiKit autostart command →
  "an AmiStudio Host already answers ... not starting another"; AmiSurf used the standalone Host (its bundled engine,
  data in `$ASB_STATE/{config,profile}`), page plus audio.
- move-to-host.sh: dry run lists 3 items; copies config, profile, tokens (0600); a second run keeps everything; an
  existing tokens file is kept.
- No `*widevine*` in the archive except get-widevine.sh; one `chrome` binary.

## Tested with Host 0.1.3 (official release, after the box reboot, 2026-10-17 20:29-20:42 EDT)
- Fresh unpack of AmiSurf-0.83-AmiKit.lha, autostart command with `--idle 1`: Host 0.1.3 from `Host/amistudio-host/`
  ("idle shutdown: the Host stops after 60 s"), engine started on connect, page plus audio (parec capture, AmiSurf "av:"
  lines, 0 underruns). The Host stayed up the whole ~3 min AmiSurf was connected and stopped exactly 60 s after the
  emulator quit ("no client for 60 s: stopping (idle shutdown)").
- run-host.sh with a non-Host listener on 9220: "taken by a program that is not an AmiStudio Host" after 2.0 s, exit 1
  (0.1.3's own status timeout; no `timeout 6` any more).
- Launcher-style Host (the standalone 0.1.3 tarball's `run-host.sh --background --idle 30 --lan`, as AmiStudio launcher
  0.1.4 starts it via Rabbit Hole): the AmiKit autostart command says "already answers ... host 0.1.3 - not starting
  another"; AmiSurf on 127.0.0.1 used it (its bundled engine, data in `$ASB_STATE/{config,profile}`), page plus audio
  (`shots/amikit-host013-reused-083-emu.png`). The launcher itself was not run (FS-UAE has no Rabbit Hole).
- Re-pair on that Host with HOST=172.30.0.2: "not paired" requester → Pair... → code → paired as 5bdbd140 with
  `[client id amiga-04003103dcb5fe55]` (`shots/amikit-host013-lan-paired-083-emu.png`); ≡ > AmiStudio Host > Pair again...
  → new code → Host: "Paired as 1589c3cb (replaces 5bdbd140)", `tokens` lists one entry, ENVARC token file changed,
  reconnected, page plus audio.
- Archive: one `chrome`, no `*widevine*` except get-widevine.sh, no "AmiStudio Browser" names; contents identical to
  the tested unpack.

## Open
- The launcher (host-client branch) reads the token but has no pairing UI yet; AmiSurf writes the same file.
- mDNS is not implemented by the Host; only the UDP broadcast.

## 0.86: identity in the drawer (AmiStudio suite rule "self-contained install folder")
ClientId, HostToken and Host come from the first directory with a `ClientId` file: `PROGDIR://Prefs/` (launcher
installed AmiSurf in `AmiStudio/Apps/AmiSurf/`), `PROGDIR:/Prefs/` (AmiSurf in the AmiStudio drawer), else
`PROGDIR:Data/` (a ClientId is made there only if none exists). A new token from Pair with Host goes into that same
directory; Forget deletes it there. Once (marker `PROGDIR:Data/Migrated`) the old `ENV:`/`ENVARC:AmiStudio/ClientId`
and `HostToken` are copied in when needed (read back to verify) and left in place, as is `ENV:AmiStudio/Host` (copied
into `PROGDIR:Data/Host` for a standalone drawer): they are shared with the AmiStudio launcher, whose 0.1.8 migrates
and deletes them. Only AmiSurf's own ENV:/ENVARC: files (AmiSurf.prefs/.window, AmiStudioBrowser.*) and the
ENV:MUI/AMISURF* files MUI made during the run are deleted.
All in `ui/asb_store.inc` (`ident_dir()`). FS-UAE test 18.10.2026: migration, launcher layout, fresh drawer, copied
drawer; ENV:/ENVARC:/T:/assigns unchanged after the runs (shots/selfcontained-086-*.png).
AmiKit edition: run-host.sh / engine.sh put HOME, XDG_*, TMPDIR (short `/tmp/amistudio-host-<uid>-<hash>` link when
`Host/data/tmp` is over 56 characters, removed when the Host exits), `--confdir` and `data_dir`/`--data-dir` under
`Host/data`. Downloads stay in the real `~/Downloads` (Rich), resolved before HOME is redirected (`ASB_USER_HOME`).
