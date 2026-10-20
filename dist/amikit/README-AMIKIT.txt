AmiSurf - AmiKit edition: AmiStudio Host + engine for Linux (x86_64)
=====================================================================

The Amiga program shows pages that a real Chromium renders on the Linux PC that runs AmiKit.
Since 0.83 this folder brings its own AmiStudio Host (amistudio-hostd), which starts that engine
(a static asb-chromed plus a portable, headless Chromium: no X server, Xvfb or packages to install)
when AmiSurf connects. It lives inside the AmiSurf drawer:

  AmiSurf/               (the Amiga drawer from the LHA)
    AmiSurf              (the Amiga program)
    Host/                         (this folder)
      run-host.sh                 starts the AmiStudio Host on 127.0.0.1:9220 (detaches, safe to run twice)
      amistudio-hostd             the AmiStudio Host 0.1.7 (amistudio-hostd from its release, unchanged)
      asb-chromed                 the engine: AmiSurf 0.88's own asb-chromed (static); Host 0.1.7's
                                  engine is 0.85's (no bookmarks bar list, site icons, hand pointer)
      chromium/                   a portable, headless ungoogled-chromium (the one Chromium in the archive)
      engine.sh                   starts that engine with this edition's data (Host/data)
      data/                       everything the Host, the engine and Chromium keep (made on first start)
      amikit-with-browser.sh      optional: start Host + AmiKit together from Linux
      move-to-host.sh             optional: copy your browser data and pairings to a standalone AmiStudio Host
      install-amikit.sh           optional: bsdsocket check + desktop entry
      get-widevine.sh             optional: Google's Widevine for DRM video (not shipped)
      LICENSES/                   AmiSurf attributions, Host version note, the Host's, Chromium's and the
                                  engine's licences

Install
  Unpack AmiSurf-<version>-AmiKit.lha in AmiKit (lha x AmiSurf-<version>-AmiKit.lha <drive>:) or on Linux
  (lha x / jlha x / lhasa x) on a drive Linux can see (a "directory" drive of your AmiKit folder, e.g.
  the one AmiKit uses for Downloads/shared files - not inside a .hdf hardfile). Start AmiSurf.
  run-host.sh puts back the exec bits LHA does not keep.
  Upgrading from 0.82 or older: unpack over the old drawer; Host/data (history, bookmarks,
  passwords, Chromium profile) is kept and used by the new engine.
  Upgrading from 0.87: unpack over the old drawer; Host/asb-chromed is replaced.
  Upgrading from 0.83/0.84/0.86: unpack over the old drawer as well. 0.87+ uses its own engine copy
  again (Host/LAYOUT says "own": Host/amistudio-hostd, Host/asb-chromed, Host/chromium/), so the
  old Host/amistudio-host/ folder (~700 MB) is no longer used and can be deleted once no Host from
  it runs (it stops by itself after the idle time).
  Upgrading from 0.85: unpack over the old drawer; its Host/chromium/ is replaced.

Everything stays in the drawer (0.86)
  The Host, the engine and Chromium keep all they write under Host/data: settings and pairing tokens
  (data/hostd), history, bookmarks, passwords (data/config), the Chromium profile (data/profile),
  other Host services (data/services), and HOME, the XDG config/data/state/cache folders and TMPDIR
  (data/home, data/tmp) for whatever Chromium, fontconfig or NSS would put in ~/.config, ~/.cache,
  ~/.pki or /tmp. In a deep drawer (data/tmp longer than 56 characters) TMPDIR is a short link
  /tmp/amistudio-host-<uid>-<number> to data/tmp while the Host runs (Chromium's socket path limit);
  it is removed when the Host stops. Copy the drawer and the Host goes with it; delete it and nothing
  is left. The one exception: downloads still go to your ~/Downloads.
  On the Amiga side AmiSurf keeps its settings in AmiSurf/Data/ too. Its AmiStudio identity
  (ClientId, HostToken) is copied once from ENVARC:AmiStudio/ into AmiSurf/Data/ (or taken from the
  AmiStudio launcher's Prefs/); the ENVARC:AmiStudio/ files themselves are left for the AmiStudio
  launcher 0.1.8, which moves and deletes them.

How the automatic start works (AmiKit's Rabbit Hole)
  AmiKit's "Rabbit Hole" runs Linux commands from the Amiga through Amiberry's Host-Tools
  (C:host-run; WinUAE editions use C:winlaunch instead, which this does not use). When
  AmiSurf finds nothing on 127.0.0.1:9220 and C:host-run exists, it runs
       C:host-run sh "<drawer>/Host/run-host.sh" --background --port 9220 --idle 30
  host-run turns the Amiga path into the Linux path. The status bar shows
  "Starting the AmiStudio Host..." for up to ~15 s, then the page appears. The Host listens on
  127.0.0.1 only, so the emulator needs no pairing. The engine and the Host stop after 30 minutes
  with no AmiSurf connected (the Host's --exit-idle).
  Before that, C:host-path checks that Linux can see the drawer; if not, the status bar says so.
  Needs: Amiberry 6+ with "Native Code" enabled (AmiKit's Rabbit Hole setting) and bsdsocket.
  Only one Host: if an AmiStudio Host already answers on 127.0.0.1:9220 - started by the AmiStudio
  launcher (0.1.4+ can start one through Rabbit Hole too), or installed from AmiStudio-Releases -
  AmiSurf uses it and does not start Host/; run-host.sh also checks ("an AmiStudio Host already
  answers ... not starting another"). That Host needs a browser service (the Host's INSTALL.md).
  If another program has port 9220, run-host.sh says so and starts nothing.

Moving to the standalone AmiStudio Host (0.1.2+)
  That Host brings its own engine and Chromium and keeps browser data in
  ~/.local/share/amistudio-host/browser (ASB_STATE overrides). To take your history, bookmarks,
  settings, passwords and the Chromium profile (cookies, logins) along, quit AmiSurf, stop both
  Hosts, then on Linux:
       sh AmiSurf/Host/move-to-host.sh --dry-run     (shows what it would copy)
       sh AmiSurf/Host/move-to-host.sh
  It copies Host/data/config and Host/data/profile there, only into an empty or missing folder
  (it never overwrites; a folder that already has data is left alone and named). Nothing is
  deleted here, so this edition keeps working. By hand it is the same as
       cp -a AmiSurf/Host/data/config AmiSurf/Host/data/profile ~/.local/share/amistudio-host/browser/
  done only while those two target folders do not exist yet. Widevine is not copied.
  Paired Amigas: Host/data/hostd/tokens is copied to ~/.config/amistudio-host/tokens
  (AMISTUDIO_HOST_DIR overrides) only if that file does not exist yet, so real Amigas paired with
  this edition (--lan) stay paired. host.conf and the logs are not copied.
  Then start the standalone Host; this edition's Host/ is not started while it answers on 9220.
  To the AmiStudio launcher's Host instead (it keeps its data inside the AmiStudio drawer):
       sh AmiSurf/Host/move-to-host.sh --to <AmiStudio drawer>/Host [--dry-run]
  copies the same into <AmiStudio>/Host/data/services/browser and data/hostd/tokens.

Tooltypes / Settings
  AUTOHOST=OFF          never start Host/run-host.sh (default: on when C:host-run is present)
  HOSTCMD=<command>     your own AmigaDOS command instead, e.g. HOSTCMD=C:host-run sh /home/me/asb/run-host.sh --background
                        For the standalone AmiStudio Host 0.1.2+ give --background too (its run-host.sh only
                        detaches with it; from 0.1.3 its --idle stops engine and Host, in 0.1.2 only the engine), e.g.
                        HOSTCMD=C:host-run sh /home/me/amistudio-host-0.1.3-linux-x86_64/run-host.sh --background --idle 30
  HOST=                 another AmiStudio Host (then Host/ is not used); only 127.0.0.1 starts Host/
  HOSTMODE=LEGACY       the 0.82 way: no Host, run-host.sh --legacy starts plain asb-chromed on 9223

By hand (no Rabbit Hole, or the drawer is on a hardfile)
  sh AmiSurf/Host/run-host.sh                 (detaches, same as --background; log below; --idle 30 to stop it when unused)
  sh AmiSurf/Host/run-host.sh --foreground    (Ctrl-C stops Host and engine)
  or AmiSurf/Host/amikit-with-browser.sh to start the Host, then AmiKit, and stop the Host (and the
  engine) when AmiKit quits (AMIKIT_CMD="..." if it cannot find AmiKit's start program).
  Real Amigas on the network too: sh run-host.sh --lan, then pair each one once:
  sh AmiSurf/Host/run-host.sh --pair prints the 6-digit code to type into AmiSurf (Pair with Host).
  Downloads: ~/Downloads (unchanged).

Where things are kept (0.77)
  AmiSurf/Host/data/ inside the drawer, created on first start (not shipped in the archive):
    config/    history.tsv, bookmarks.tsv, settings, passwords
    profile/   the Chromium profile (cookies, cache, logins)
    host.log   run-host.sh and the AmiStudio Host's log (Linux side)
    hostd/     the bundled Host's settings (host.conf is rewritten by run-host.sh), tokens of paired
               Amigas (--lan) and browser.log, the engine log
  AmiSurf.log  next to the AmiSurf program: the Amiga-side log (see README).
  Upgrading from 0.76 or older: the first start moves config/, profile/ and host.log over from
  ~/.local/state/amisurf-amikit/ and leaves MOVED-TO-DRAWER.txt there; host.log says so.
  If the drawer is read-only, mounted noexec or cannot hold symlinks (Chromium needs them for its
  profile lock), the engine keeps using ~/.local/state/amisurf-amikit/ and host.log says why.
  ASB_STATE=/some/folder sh run-host.sh puts it somewhere else. The bundled Host does not use
  ~/.config/amistudio-host (that belongs to an installed AmiStudio Host). Back up the drawer = back up everything.

Exec bits
  LHA, zip and Amiga-side copies can drop the Unix "executable" flags. run-host.sh is always started
  with "sh", and it restores the flags on asb-chromed and Chromium itself (and says so in host.log).
  If the drive is mounted noexec, copy Host/ to your Linux home and use HOSTCMD=.

Networking
  bsdsocket.library emulation must be on (AmiKit has networking on by default).
  install-amikit.sh /path/to/AmiKit sets bsdsocket_emu=true in AmiKit's .uae configs (each changed file is
  backed up first as <file>.asb-backup-<date>) and adds the desktop entry. Host and engine only listen on
  127.0.0.1 (unless run-host.sh --lan).

Sound
  The engine plays Chromium into its own PulseAudio/PipeWire sink and streams it to the Amiga.
  Needs pactl and parec (package pulseaudio-utils; PipeWire desktops have pipewire-pulse) or ffmpeg.
  No sound server running? If the pulseaudio program is installed, the engine starts one.
  hostd/browser.log says "audio sink asb_<pid>" when sound works, or why not. ASB_AUDIO=0 turns it off.

DRM video (Netflix, Spotify, Prime Video, Disney+) - 0.80
  These sites need Google's Widevine CDM. It belongs to Google and may not be redistributed, so it is
  not in this archive. Add it once on Linux:  sh AmiSurf/Host/get-widevine.sh
  It copies the CDM from Google Chrome if Chrome is installed on this PC, or (after asking) downloads
  Google's Chrome package from dl.google.com and takes only its WidevineCdm folder; it lands in
  Host/data/WidevineCdm. With Google Chrome installed the engine also finds Chrome's CDM by itself.
  Linux browsers get Widevine L3 (software), so services cap the quality: Netflix up to 1080p (often
  720p), Disney+ 480p-720p, Prime Video SD. Without the CDM Netflix shows error M7701-1003.

Requirements and limits
  * x86_64 Linux, glibc 2.25+, the usual desktop libraries (NSS, GTK/ATK, ALSA, CUPS, libgbm);
    if one is missing, host.log names it (run-host.sh checks before it starts the Host).
  * Ubuntu 23.10+/24.04: if hostd/browser.log shows "No usable sandbox", AppArmor blocks Chromium's sandbox;
    run once: sudo sysctl -w kernel.apparmor_restrict_unprivileged_userns=0. The sandbox stays on.
  * Chrome backend only; headless, so no Chromium window on the Linux desktop.

Licences: Chromium is BSD-licensed (LICENSES/Chromium-LICENSE.txt) with third-party components
(LICENSES/chromium-credits.html); ungoogled-chromium and its portable build are BSD-3-Clause.
Source: https://github.com/ungoogled-software/ungoogled-chromium-portablelinux/releases/tag/155.0.8059.79-1
