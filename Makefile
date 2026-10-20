# AmiSurf — Chrome-backend edition.
#   asb-chromed : Linux host; drives installed Chrome over CDP and serves the ASBC protocol (port 9223)
#   asb-sdl     : Linux thin client (SDL2)
#   AmiSurf          : 68k MUI client, 68040/060 (hard FPU)        -- primary
#   AmiSurf-020      : 68k MUI client, 68020/030 + 68881/68882 FPU (FPU check at startup)
#   AmiSurf-gadtools : 68k Intuition/GadTools client, 68020+FPU (no MUI needed) -- fallback
# The old in-house HTML/CSS/JS/MintVID engine lives at tag classic-final / branch classic.
HOSTCC    = cc
HOSTCFLAGS= -O2 -g -Wall -Wno-misleading-indentation -std=gnu99 -pthread
AMIGACC   = /opt/amiga/bin/m68k-amigaos-gcc
AMIGABASE = -noixemul -Wall -Wno-misleading-indentation -Wno-incompatible-pointer-types -std=gnu99 -D__AMIGA__ -include tools/sfdc_vararg.h
CF040 = -O2 -m68040 -m68881 $(AMIGABASE)
CF020 = -O2 -m68020 -m68881 -ffloat-store -fexcess-precision=standard -DASP_020 $(AMIGABASE)
CFCPU = -O2 -m68020 -msoft-float $(AMIGABASE)
MUIFLAGS = -Iui/muiinc -DASP_MUI -Wno-pointer-sign -Wno-attributes -Wno-unused-variable
AMIGA_SRC = ui/asb_gfx_chrome.c ui/asb_theme.inc ui/asb_ash.inc ui/asb_store.inc sys/rtg_amiga.h $(PROTO) ui/asb_audio.inc ui/asb_log.inc ui/asb_rexx.inc ui/asb_bmarks.inc ui/asb_bmbar_gt.inc
PROTO     = chrome/asbc.c chrome/asbc.h chrome/qoi.h

all: host amiga
host: build/host/asb-chromed build/host/asb-sdl
AMIGA_BINS = build/amiga/AmiSurf build/amiga/AmiSurf-020 build/amiga/AmiSurf-gadtools
amiga: $(AMIGA_BINS)
chrome-host: build/host/asb-chromed
sdl: build/host/asb-sdl

build/host/asb-chromed: chrome/host.c chrome/features.inc chrome/frames.inc chrome/bidi.inc chrome/audio.inc chrome/favicon.inc $(PROTO)
	@mkdir -p $(@D); $(HOSTCC) $(HOSTCFLAGS) -o $@ chrome/host.c chrome/asbc.c -lcrypto -lm
build/host/asb-sdl: ui/asb_sdl_chrome.c $(PROTO)
	@mkdir -p $(@D); $(HOSTCC) $(HOSTCFLAGS) -I/usr/include/SDL2 -o $@ ui/asb_sdl_chrome.c chrome/asbc.c -lSDL2 -lSDL2_ttf -lm
build/amiga/cpucheck-040.o: sys/cpucheck.c
	@mkdir -p $(@D); $(AMIGACC) $(CFCPU) -c -o $@ $<
build/amiga/cpucheck-020.o: sys/cpucheck.c
	@mkdir -p $(@D); $(AMIGACC) $(CFCPU) -DASP_020 -c -o $@ $<
build/amiga/AmiSurf: ui/asb_mui_chrome.c ui/asb_mui.inc $(AMIGA_SRC) build/amiga/cpucheck-040.o
	@mkdir -p $(@D); $(AMIGACC) $(CF040) $(MUIFLAGS) -o $@ ui/asb_mui_chrome.c chrome/asbc.c build/amiga/cpucheck-040.o -lm -lamiga
build/amiga/AmiSurf-020: ui/asb_mui_chrome.c ui/asb_mui.inc $(AMIGA_SRC) build/amiga/cpucheck-020.o
	@mkdir -p $(@D); $(AMIGACC) $(CF020) $(MUIFLAGS) -o $@ ui/asb_mui_chrome.c chrome/asbc.c build/amiga/cpucheck-020.o -lm -lamiga
build/amiga/AmiSurf-gadtools: $(AMIGA_SRC) build/amiga/cpucheck-020.o
	@mkdir -p $(@D); $(AMIGACC) $(CF020) -o $@ ui/asb_gfx_chrome.c chrome/asbc.c build/amiga/cpucheck-020.o -lm -lamiga

# 0.88: test tool: runs a program on a stack the task's tc_SPLower/tc_SPUpper do not describe (a beta tester's 0.87 Gurus)
build/amiga/badstack: test/badstack.c
	@mkdir -p $(@D); $(AMIGACC) -O2 -m68020 $(AMIGABASE) -Wno-format -o $@ $<

# 0.88: test tool: a right mouse click on request (FS-UAE runs; see the file)
build/amiga/rmbclick: test/rmbclick.c
	@mkdir -p $(@D); $(AMIGACC) -O2 -m68020 $(AMIGABASE) -Wno-format -o $@ $<

# protocol + QOI round-trip test
build/host/asbc_test: test/asbc_test.c $(PROTO)
	@mkdir -p $(@D); $(HOSTCC) $(HOSTCFLAGS) -o $@ test/asbc_test.c chrome/asbc.c
test: build/host/asbc_test
	build/host/asbc_test

# integration test of the host features (starts its own Chrome; needs an X display)
test-host: build/host/asb-chromed
	bash test/host_features.sh
test-host-firefox: build/host/asb-chromed
	ASB_TEST_BROWSER=firefox bash test/host_features.sh

# release archive (needs jlha/lha that can create archives)
VERSION := $(shell sed -n 's/^\#define ASB_VERSION "\(.*\)"/\1/p' ui/asb_gfx_chrome.c)
lha: amiga dist/docs/README dist/docs/CHANGES
	sh dist/mkarchive.sh $(VERSION)
	cd dist && sha256sum AmiSurf-$(VERSION).lha > SHA256SUMS-$(VERSION) && cat SHA256SUMS-$(VERSION)

# AmiKit edition: the Amiga drawer with the Linux engine in AmiSurf/Host (run make amikit-bundle first)
lha-amikit: amiga
	ASB_HOSTDIR=build/amisurf-amikit-host-$(VERSION)-linux-x86_64/AmiSurf/Host sh dist/mkarchive.sh $(VERSION) $(CURDIR)/dist/AmiSurf-$(VERSION)-AmiKit.lha
	cd dist && sha256sum AmiSurf-$(VERSION)-AmiKit.lha > SHA256SUMS-$(VERSION)-AmiKit && cat SHA256SUMS-$(VERSION)-AmiKit

clean:
	rm -rf build
.PHONY: all host amiga chrome-host sdl test lha clean

# static asb-chromed (AmiKit bundle; also a portable engine for the AmiStudio Host's "browser" service)
build/static/asb-chromed: chrome/host.c chrome/features.inc chrome/frames.inc chrome/bidi.inc chrome/audio.inc chrome/favicon.inc $(PROTO)
	@mkdir -p $(@D); $(HOSTCC) -O2 -std=gnu99 -pthread -static -o $@ chrome/host.c chrome/asbc.c -lcrypto -lz -lzstd -lm 2>&1 | grep -v "warning: Using" || true; test -x $@
# 0.82: the own Linux host package (asb-chromed-<ver>-linux-x86_64.tar.gz, dist/host/) is retired; the AmiStudio Host
# runs asb-chromed (docs/HOST.md). The last one is asb-chromed-0.77-linux-x86_64.tar.gz.
host-tarball:
	@echo "retired in 0.82: install the AmiStudio Host and mount build/static/asb-chromed as its browser service (docs/HOST.md)"; exit 1

# AmiKit host bundle (static asb-chromed + portable ungoogled-chromium, headless); too big for git, see dist/amikit
UCVER = 155.0.8059.79-1
UCDIR = build/uc/ungoogled-chromium-$(UCVER)-x86_64_linux
$(UCDIR)/chrome:
	mkdir -p build/uc && curl -fL -o build/uc/uc.tar.xz https://github.com/ungoogled-software/ungoogled-chromium-portablelinux/releases/download/$(UCVER)/ungoogled-chromium-$(UCVER)-x86_64_linux.tar.xz && tar -C build/uc -xf build/uc/uc.tar.xz
# 0.83: the bundle carries the official AmiStudio Host release, checked against its SHA-256 (on download and again in
# mkbundle.sh). 0.86: Host 0.1.7 (its engine is AmiSurf 0.85's e762b82 + the Host's engine patches)
HOSTVER ?= 0.1.7
HOSTSHA_0.1.7 = 5ffdb4fc86d85d7f9102152e7dd412ea6729ec306c4861b41625cae86fc1d020
HOSTSHA_0.1.6 = 0f5aa54c0f05486986506df8bc4bcf4c5745b89e2deb0eeca001d352e698d865
HOSTSHA ?= $(HOSTSHA_$(HOSTVER))
# host: use the Host release's engine + Chromium (one copy); own: the edition's own asb-chromed + Chromium in Host/
# 0.85 was own (the Host 0.1.6 engine lacked the 0.85 download/tab fixes); 0.86: host again (Host 0.1.7 has them)
# 0.87: own again (Host 0.1.7's engine is 0.85's: no bookmarks list for the 0.87 bookmarks bar)
AMIKIT_CHROMIUM ?= own
HOSTTGZ = build/hostrel/amistudio-host-$(HOSTVER)-linux-x86_64.tar.gz
$(HOSTTGZ):
	@test -n "$(HOSTSHA)" || { echo "no SHA-256 for Host $(HOSTVER): set HOSTSHA="; exit 1; }
	mkdir -p $(@D) && curl -fL -o $@.part https://github.com/hardcopi/AmiStudio-Releases/releases/download/host-v$(HOSTVER)/$(@F) && echo "$(HOSTSHA)  $@.part" | sha256sum -c - && mv $@.part $@
AMIKIT_DEPS_host =
AMIKIT_DEPS_own = build/static/asb-chromed $(UCDIR)/chrome
amikit-bundle: $(AMIKIT_DEPS_$(AMIKIT_CHROMIUM)) $(HOSTTGZ)
	bash dist/amikit/mkbundle.sh $(VERSION) $(UCDIR) dist/amikit/licenses $(HOSTTGZ) $(HOSTSHA) $(AMIKIT_CHROMIUM)
