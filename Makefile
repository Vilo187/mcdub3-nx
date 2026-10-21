# Midnight Club 3 recomp: the development loop, from the repo root.
#
#   make            this help
#   make lift       first lift (or after reinstalling the disc)
#   make build run  the everyday loop
#
# Targets wrap the scripts in tools/, switch/ and vm/ (which also work alone).
# Settings pass straight through, e.g.
#   make run SECS=60 RECOMP_KEYBOARD=1
#   make build VULKAN=1 JOBS=4
# Logs go to work/logs/.

SHELL := /bin/bash
.SHELLFLAGS := -eo pipefail -c
.DEFAULT_GOAL := help

WORK := $(or $(MC3_WORK),$(CURDIR)/work)
LOGS := $(WORK)/logs
NOW   = $(shell date +%Y%m%d_%H%M%S)

# Command-line settings that the scripts read from the environment.
export VULKAN DEBUG GEN_DEBUGINFO JOBS SECS HEADLESS GDB LTO LTO_JOBS NO_GAME WINDOW VIDEO KEEP EVERY NATIVE KEYS TRACE \
       MC3_WORK MC3_GAME_DIR MC3_MMIO_SECTIONS MC3_RECOMP_ARGS MC3_DISASM_ARGS FACTOR MIN FILES HD SCALE SETTINGS GAME

.PHONY: help setup nsp check-vm extract lift regen relift \
        build build-vk build-debug mac-app run run-gdb run-headless snapshot report hoststacks profile record difftest trap \
        switch switch-vk switch-lto send \
        hd-dump hd-tools hd-upscale \
        seeds lift-report log du clean-build clean-gen clean toolkit-test

help:  ## Show this help
	@awk 'BEGIN {FS = ":.*## "} /^## / {printf "\n%s\n", substr($$0, 4)} \
	     /^[a-z-]+:.*## / {printf "  make %-14s %s\n", $$1, $$2}' $(MAKEFILE_LIST)
	@echo

## Setup (once)
setup:  ## Install build deps, devkitPro, Python venv (VM)
	bash vm/setup-ubuntu.sh

check-vm:  ## Print the VM checks (CPU, page size, RAM, disk, OpenGL)
	@uname -m; echo "page size: $$(getconf PAGESIZE)"; echo "cores: $$(nproc)"
	@free -g | head -2; df -h . | tail -1
	@glxinfo -B 2>/dev/null | grep -E 'renderer|core profile version' || echo "glxinfo: install mesa-utils"

extract:  ## Unpack an ISO into game/: make extract ISO=<file.iso>
	@test -n "$(ISO)" || { echo "usage: make extract ISO=<file.iso>"; exit 1; }
	bash tools/extract_disc.sh "$(ISO)"

## Lift (XBE -> C in work/gen)
lift:  ## Parse the XBE, write config/xbe_facts.h, full lift
	@mkdir -p $(LOGS)
	bash tools/first_lift.sh 2>&1 | tee $(LOGS)/lift_$(NOW).log

regen:  ## Full regen: after changing config/seed_functions.json
	@mkdir -p $(LOGS)
	bash tools/regen.sh 2>&1 | tee $(LOGS)/regen_$(NOW).log

relift:  ## Lift only: after adding/removing an override, or a translator change (TRACE=XACTENG,DSOUND: failure trace)
	@mkdir -p $(LOGS)
	LIFT_ONLY=1 bash tools/regen.sh 2>&1 | tee $(LOGS)/relift_$(NOW).log

seeds:  ## Propose seeds from a run log: make seeds LOG=work/logs/run_x.log (review, then make regen)
	@test -n "$(LOG)" || { echo "usage: make seeds LOG=<run log>"; exit 1; }
	cd xboxrecomp && "$(WORK)/venv/bin/python3" -m tools.seed_from_log "$(abspath $(LOG))" \
	    "$(or $(MC3_GAME_DIR),$(CURDIR)/game)/default.xbe" \
	    --functions "$(WORK)/analysis/disasm/functions.json" \
	    --seeds "$(CURDIR)/config/seed_functions.json"
	@echo "review the additions: git diff config/seed_functions.json"

lift-report:  ## Summarise the last lift (functions, stubs, unimplemented)
	@grep -E '^(Poll loops|MMIO|[0-9]+/[0-9]+ functions|[0-9]+ unresolved|[0-9]+ instruction)' $(WORK)/analysis/recomp.log \
	    || echo "no lift yet: make lift"

## Linux (the debug loop)
build:  ## Build the Linux binary (OpenGL)
	@mkdir -p $(LOGS)
	bash tools/build_linux.sh 2>&1 | tee $(LOGS)/build_$(NOW).log

build-vk:  ## Build with the Vulkan renderer (needs glslang 12+)
	$(MAKE) build VULKAN=1

build-debug:  ## Build hand-written code at -O0 for gdb (+ GEN_DEBUGINFO=1 for lifted C)
	$(MAKE) build DEBUG=1

run:  ## Run in a window; keyboard = pad 1 (SECS=n stops after n s; WATCH=1: F12 logs every draw of that frame)
	$(if $(WATCH),RECOMP_GL_WATCH=all RECOMP_GL_WATCH_FRAME=key) bash tools/run_linux.sh

run-gdb:  ## Run under gdb (bt = host chain; call print_guest_state())
	GDB=1 bash tools/run_linux.sh

run-headless:  ## Run without a window (pair with RECOMP_GL_DUMP / RECOMP_PAD_SCRIPT)
	HEADLESS=1 bash tools/run_linux.sh

snapshot:  ## Diagnose a hang: headless run, guest stack dumped after 30 s (WD=n to change)
	@HEADLESS=1 SECS=$${SECS:-90} RECOMP_WATCHDOG_SECS=$(or $(WD),30) bash tools/run_linux.sh; \
	 rc=$$?; if [ $$rc -eq 3 ]; then echo "snapshot taken (watchdog exit 3 is expected): make report"; \
	 elif [ $$rc -ne 0 ]; then echo "run ended with exit code $$rc (not the watchdog): make report"; exit $$rc; fi

difftest:  ## Replay call CALL of wrapped function FUNC on x86 (Unicorn) vs the lifted C (FUNC default 0037CCD0)
	@test -n "$(CALL)" || { echo "usage: make difftest CALL=<n> [FUNC=0037CCD0] [WITHIN=<outer func>:<n>]"; exit 1; }
	@mkdir -p $(LOGS)
	MC3_DIFFTEST_CALL=$(CALL) MC3_DIFFTEST_FUNC=$(or $(FUNC),0037CCD0) MC3_DIFFTEST_WITHIN=$(WITHIN) \
	    HEADLESS=1 SECS=$${SECS:-300} bash tools/run_linux.sh || true
	@py=; for p in $(WORK)/venv/bin/python3 python3; do $$p -c 'import numpy, unicorn, capstone' 2>/dev/null && { py=$$p; break; }; done; \
	 if [ -z "$$py" ]; then echo "captured in $(WORK)/difftest; no Python with numpy+unicorn here:"; \
	   echo "  $(WORK)/venv/bin/pip install numpy unicorn   (or replay on another machine)"; exit 0; fi; \
	 $$py tools/difftest.py $(WORK)/difftest/$(or $(FUNC),0037CCD0)_$(CALL)_before.bin 2>&1 | tee $(LOGS)/difftest_$(NOW).log

record:  ## Record frames + audio into work/capture/<time>/ (SECS, EVERY, KEEP=1, WINDOW=1, VIDEO=0)
	bash tools/record.sh $(or $(SECS),60)

profile:  ## Where the time goes: perf sampling of a headless run (SECS, default 40)
	bash tools/profile.sh $(or $(SECS),40)

hoststacks:  ## Every thread's host backtrace after SECS (default 45): who waits on what
	bash tools/host_stacks.sh $(or $(SECS),45)

report:  ## Explain the snapshot in the newest run log (LOG=<file> for another)
	@python3 tools/snapshot_report.py $(LOG)

trap:  ## Run, stopping at the first untranslated instruction reached
	RECOMP_UNIMPL_TRAP=1 bash tools/run_linux.sh

## macOS app

mac-app:  ## Package the Mac build as work/mac_app/Midnight Club 3.app (SCALE=1.5, SETTINGS="K=V ...", HD=1, GAME=<disc dir>)
	@mkdir -p $(LOGS)
	$(MAKE) build
	bash tools/mac_app.sh 2>&1 | tee $(LOGS)/mac_app_$(NOW).log

## Texture packs (hd/ beside game/; xboxrecomp/src/nv2a_gl/hd_pack.c)

hd-dump:  ## Run with every texture written once to hd/dump/<hash>.png (options as for run)
	RECOMP_GL_TEX_DUMP=1 $(MAKE) run

hd-tools:  ## Fetch the upscaler (Real-ESRGAN) for this OS into work/tools (macOS, x86-64 Linux)
	bash tools/hd_tools.sh

hd-upscale:  ## hd/dump -> hd/textures, new ones only (FACTOR=2|3|4, MIN=px, FILES="... " to redo some)
	@mkdir -p $(LOGS)
	bash tools/hd_upscale.sh $(or $(FACTOR),2) 2>&1 | tee $(LOGS)/hd_upscale_$(NOW).log

## Switch
switch:  ## Build the NRO in devkitPro's pinned container (switch/devkitpro-image) and stage work/switch_sd; HD=1: with hd/textures; NATIVE=1: local devkitPro
	@mkdir -p $(LOGS)
	bash switch/$(if $(filter 1,$(NATIVE)),build.sh,docker_build.sh) 2>&1 | tee $(LOGS)/switch_$(NOW).log

nsp:  ## The NSP forwarder (home-menu app) from switch/app.env, in the container; KEYS=keys/prod.keys
	@mkdir -p $(LOGS)
	bash switch/nsp.sh prepare 2>&1 | tee $(LOGS)/nsp_$(NOW).log
	bash switch/$(if $(filter 1,$(NATIVE)),nsp.sh,docker_build.sh switch/nsp.sh) 2>&1 | tee -a $(LOGS)/nsp_$(NOW).log

switch-vk:  ## Switch build with Vulkan (NVK)
	$(MAKE) switch VULKAN=1

switch-lto:  ## Switch build with link-time optimisation
	$(MAKE) switch LTO=1

send:  ## Send the NRO over nxlink (hbmenu, press Y) and log the game to work/logs/switch_*.log; SWITCH_IP=a.b.c.d, NATIVE=1
	@mkdir -p $(LOGS)
	bash switch/send.sh 2>&1 | tee $(LOGS)/switch_run_$(NOW).log

## Housekeeping
log:  ## Show the end of the newest log in work/logs
	@f=$$(ls -t $(LOGS)/*.log 2>/dev/null | head -1); \
	 if [ -n "$$f" ]; then echo "== $$f"; tail -40 "$$f"; else echo "no logs yet"; fi

du:  ## Disk use of work/ and game/
	@du -sh $(WORK)/* game 2>/dev/null | sort -h

toolkit-test:  ## Run the toolkit's Python unit tests
	cd xboxrecomp && "$(WORK)/venv/bin/python3" -m pytest -q tools/

clean-build:  ## Delete build trees (keeps the lift)
	rm -rf $(WORK)/build-*

clean-gen:  ## Delete the lifted C and analysis (next: make lift)
	rm -rf $(WORK)/gen $(WORK)/analysis

clean: clean-build clean-gen  ## Delete everything generated except the venv and logs
