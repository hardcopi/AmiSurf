# AGENTS.md

Read DEVELOPING.md first: setup (`scripts/setup.sh`), build, tests, emulator, module layout, roadmap and rules.

This repo is the **Chrome-backend** browser: `asb-chromed` (Linux, drives Chrome over CDP) + thin clients
(`AmiSurf` 68k, `asb-sdl` Linux) talking ASBC. The old in-house engine is at tag `classic-final` / branch
`classic`; don't re-add it to `main`.

Quick rules for coding agents:
* Build and test on Linux first: `make host && make test`; check pages with `asb-chromed` + `asb-sdl -shot`.
* Then cross-build for Amiga (`make amiga`) and, if an emulator is set up, verify there (`emu/run68.sh`).
* Never add ROMs, AmigaOS files, emulator images, cookies, Chrome profiles or credentials to git. `emu/hd/` is local only.
* Versions go 0.66 → 0.67 → ...; 1.0 means full-featured. Update CHANGES/README with each release.
* Priorities: YouTube on 68k, then Gmail.
* This checkout lives in the AmiKit workspace. Install 68k binaries to `../../AmiKit/AmiStudio/AmiSurf/` (keep icons, LICENSES, about-banner). Workspace rules: `~/amikit/AGENTS.md` if you keep one.
