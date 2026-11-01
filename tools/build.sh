#!/bin/sh
# Build both plugins with the Opus plugin kit's own tool, then read each back as the core will:
#   OPUS_KIT=/path/to/opus/plugin-kit PATH=/path/to/bfin/bin:$PATH sh tools/build.sh [OUT_DIR]
#   -> OUT_DIR/sammi.reverb.opmod, OUT_DIR/sammi.stutter.opmod (default build/opus)
# OPUS_KIT: the plugin-kit folder of https://github.com/LizardMan245/opus. The compiler is found as the kit finds it
# (OP1_BFIN_TOOLCHAIN, a bfin-toolchain folder beside the kit, or bfin-elf-gcc on PATH); tools/toolchain builds one.
# Copyright (C) 2026 sammi. GPL-3.0-or-later with the OP-1 firmware permission.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
KIT=${OPUS_KIT:?set OPUS_KIT to the Opus plugin kit (github.com/LizardMan245/opus, plugin-kit)}
OUT=${1:-$ROOT/build/opus}
mkdir -p "$OUT"
for p in reverb stutter; do
    python3 "$KIT/plugin_tool.py" build "$ROOT/plugins/sammi.$p" --out "$OUT/sammi.$p.opmod"
    python3 "$KIT/plugin_tool.py" show "$OUT/sammi.$p.opmod"
done
cd "$OUT" && sha256sum sammi.reverb.opmod sammi.stutter.opmod
