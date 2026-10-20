# AmiStudio Browser → AmiSurf (0.82)

Rich, 2026-10-17: the browser is now **AmiSurf**, an app of the **AmiStudio** suite (the suite keeps its name).
Same release as the move to the AmiStudio Host (`docs/HOST.md`). The GitHub repo keeps its name
(`amistudio-browser`); so do the wire protocol (ASBC), the engine binary (`asb-chromed`) and source file names (`asb_*`).

## What changes

| what | before (≤ 0.81) | from 0.82 |
|---|---|---|
| display name (window/screen titles, About, menus, requesters, splash, status) | AmiStudio Browser | AmiSurf |
| version string | `$VER: AmiStudioBrowser 0.81 (…)` | `$VER: AmiSurf 0.82 (…)` |
| binaries | AmiStudioBrowser, -020, -gadtools | AmiSurf, AmiSurf-020, AmiSurf-gadtools |
| icons | AmiStudioBrowser*.info, drawer AmiStudioBrowser.info | AmiSurf*.info, AmiSurf.info (same art) |
| drawer in the archive | AmiStudioBrowser/ | AmiSurf/ |
| archive | AmiStudioBrowser-<ver>.lha, …-AmiKit.lha | AmiSurf-<ver>.lha, AmiSurf-<ver>-AmiKit.lha (+ SHA256SUMS) |
| release | public browser releases repo, tag v<ver> | hardcopi/AmiStudio-Releases, tag `amisurf-v<ver>`, manifest id `amisurf` (published by `tools/publish.sh` there, only with Rich's OK) |
| MUI application base (ARexx/Commodities name) | AMISTUDIOBROWSER | AMISURF |
| prefs | ENV(ARC):AmiStudioBrowser.prefs | ENV(ARC):AmiSurf.prefs |
| window position | ENV(ARC):AmiStudioBrowser.window | ENV(ARC):AmiSurf.window |
| log | PROGDIR:AmiStudioBrowser.log (+ .old) | PROGDIR:AmiSurf.log (+ .old) |
| engine data on Linux | ~/.config/amistudio-browser | ~/.config/amisurf |
| PulseAudio sink description, download user agent | AmiStudioBrowser | AmiSurf |
| Linux SDL test client title | AmiStudio Browser | AmiSurf |

## Migration (old names keep working)
- **Prefs and window position**: if no `AmiSurf.prefs`/`.window` exists, the old `AmiStudioBrowser.*` file is read;
  the next save writes the new name only. The old files are left alone, so going back to 0.81 still works.
- **Tooltypes**: unchanged (THEME, SCREENMODE, STATS, SOUND, LOG …); new: `HOST=`, `HOSTMODE=`, `SERVICE=` (Host).
  Icons from 0.81 can be copied over: their tooltypes still apply.
- **Log**: new name only; an old `AmiStudioBrowser.log` is not touched.
- **Engine data** (bookmarks, history, passwords, settings, Chrome profile): if `~/.config/amisurf` does not exist
  but `~/.config/amistudio-browser` does, the engine keeps using the old folder (logged), so nothing is lost.
  `ASB_CONFIG=` still overrides.

## Not renamed (on purpose)
- Repo, branch names, source files, `asb-chromed`, ASBC, `ASB_*` environment variables and build macros — internal,
  and the AmiStudio Host's `service browser` block refers to `asb-chromed`.
- The Host service name stays `browser` (Host's `host.conf`, launcher manifest `host browser`).
- History: CHANGES entries before 0.82, shots.

## Packaging
- `make lha` → `dist/AmiSurf-<ver>.lha` + `dist/AmiSurf-<ver>.lha.sha256`; `make sha256sums` writes
  `dist/SHA256SUMS-<ver>` in the Releases repo format (`sha256  filename`). Archive top level: `AmiSurf.info` +
  `AmiSurf/` (incl. `Icons/about.png`, the manifest's `icon`).
- The own Linux host packages (`asb-chromed-<ver>-linux-x86_64.tar.gz`, `dist/host/`) are retired: the engine comes
  with / is run by the AmiStudio Host (`docs/HOST.md`). The AmiKit edition (Linux engine inside the drawer, legacy mode)
  is renamed only (AmiSurf/Host/…); it is not rebuilt in this change.
