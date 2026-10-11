#!/usr/bin/env bash
# Regenerate the lifted C from the XBE:
#   xbe_parser -> disasm -> func_id -> abi_analysis -> recomp
#
#   tools/regen.sh                  full run
#   LIFT_ONLY=1 tools/regen.sh      recomp only: enough after editing
#                                   src/recomp_manual.c or the translator;
#                                   seed changes need the full run
#
# Analysis goes to $MC3_ANALYSIS_DIR and the C to $MC3_GEN_DIR, both outside
# the repo (tools/env.sh), so the vendored toolkit tree stays clean.
#
# Seeds (config/seed_functions.json) are entry points the static pass cannot
# see: thread start routines and indirect-call targets found at runtime. Grow
# them from a run log with the toolkit's tools.seed_from_log and review every
# addition -- a seed inside a function splits it.
#
# MC3_MMIO_SECTIONS: XBE sections whose code drives hardware registers
# (NFSU2 used DSOUND,XPP). Lifted with MMIO accessors so devices work without
# fault trapping. Set after reading the section list from the first parse.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"

SEEDS="$MC3_REPO/config/seed_functions.json"
MANUAL="$MC3_REPO/src/recomp_manual.c"
MMIO="${MC3_MMIO_SECTIONS:-DSOUND,XPP}"
A="$MC3_ANALYSIS_DIR"

[ -f "$MC3_XBE" ] || mc3_fail "no XBE at $MC3_XBE (tools/extract_disc.sh)"
mkdir -p "$A/disasm" "$A/func_id" "$A/abi" "$MC3_GEN_DIR"
cd "$XBOXRECOMP_DIR"

# disasm reads <xbe stem>_analysis.json beside the XBE.
XBE_JSON="${MC3_XBE%.xbe}_analysis.json"

if [ "${LIFT_ONLY:-0}" != "1" ]; then
    python3 -m tools.xbe_parser "$MC3_XBE" --json "$XBE_JSON" --quiet
    cp "$XBE_JSON" "$A/xbe_analysis.json"
    python3 -m tools.disasm "$MC3_XBE" -o "$A/disasm" \
        --analysis-json "$XBE_JSON" --seed-functions "$SEEDS" \
        ${MC3_DISASM_ARGS:-}
    python3 -m tools.func_id "$MC3_XBE" -o "$A/func_id" \
        --functions "$A/disasm/functions.json" \
        --strings "$A/disasm/strings.json" \
        --xrefs "$A/disasm/xrefs.json"
    python3 -m tools.abi_analysis "$MC3_XBE" --output-dir "$A/abi" \
        --disasm-dir "$A/disasm" --func-id-dir "$A/func_id"
fi

# recomp_types.h is written only when absent, so a stale copy would hide
# register-model changes in the toolkit. Nothing edits it by hand.
rm -f "$MC3_GEN_DIR/recomp_types.h"
python3 -m tools.recomp "$MC3_XBE" --game-name "Midnight Club 3: DUB Edition" \
    --all --split 250 --gen-dir "$MC3_GEN_DIR" \
    --disasm-dir "$A/disasm" --func-id-dir "$A/func_id" --abi-dir "$A/abi" \
    --exclude-manual "$MANUAL" --mmio-sections "$MMIO" \
    ${MC3_RECOMP_ARGS:-} 2>&1 | tee "$A/recomp.log"

echo
echo "lifted C in $MC3_GEN_DIR: $(find "$MC3_GEN_DIR" -name '*.c' | wc -l) files, $(du -sh "$MC3_GEN_DIR" | cut -f1)"
# The translator ends with its tally of instructions it could not lift (the
# honest coverage signal); the whole run is kept in recomp.log.
echo "recomp log: $A/recomp.log"
