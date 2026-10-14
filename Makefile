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
export VULKAN DEBUG GEN_DEBUGINFO JOBS SECS HEADLESS GDB LTO LTO_JOBS NO_GAME \
       MC3_WORK MC3_GAME_DIR MC3_MMIO_SECTIONS MC3_RECOMP_ARGS MC3_DISASM_ARGS

.PHONY: help setup check-vm extract lift regen relift \
        build build-vk build-debug run run-gdb run-headless trap \
        switch switch-vk switch-lto send \
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

relift:  ## Lift only: after adding/removing an override, or a translator change
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

run:  ## Run in a window (SECS=n to stop after n seconds)
	bash tools/run_linux.sh

run-gdb:  ## Run under gdb (bt = host chain; call print_guest_state())
	GDB=1 bash tools/run_linux.sh

run-headless:  ## Run without a window (pair with RECOMP_GL_DUMP / RECOMP_PAD_SCRIPT)
	HEADLESS=1 bash tools/run_linux.sh

trap:  ## Run, stopping at the first untranslated instruction reached
	RECOMP_UNIMPL_TRAP=1 bash tools/run_linux.sh

## Switch
switch:  ## Build the NRO and stage work/switch_sd
	@mkdir -p $(LOGS)
	bash switch/build.sh 2>&1 | tee $(LOGS)/switch_$(NOW).log

switch-vk:  ## Switch build with Vulkan (NVK)
	$(MAKE) switch VULKAN=1

switch-lto:  ## Switch build with link-time optimisation
	$(MAKE) switch LTO=1

send:  ## Send the NRO to the console over nxlink (hbmenu, press Y)
	nxlink -s $(WORK)/switch_sd/switch/mcdub3/mc3_recomp$(if $(filter 1,$(VULKAN)),-vulkan,).nro

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
