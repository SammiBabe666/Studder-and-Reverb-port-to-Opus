#!/bin/sh
# Every check, the build and the release in one go:
#   KENNEL=.../hundekraft-3.1.1-source/ext/kennel OPUS_KIT=.../opus/plugin-kit BFIN_RUN=.../bfin/run \
#       PATH=/path/to/bfin/bin:$PATH sh tools/release.sh
# 1. tests/host/run.sh (ASan + UBSan: the ports against Hundekraft's originals, the port checks, the analysis)
# 2. tests/bfin_sim/run.sh (the compiled ports in the Blackfin simulator against the originals; cycles)
# 3. tools/build.sh (the .opmod files with the Opus kit)
# 4. results/ refreshed, release/ assembled (tools/make_release.py)
# Copyright (C) 2026 sammi. GPL-3.0-or-later with the OP-1 firmware permission.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$ROOT/results"
sh "$ROOT/tests/host/run.sh" "$ROOT/build/host" | tee "$ROOT/results/host_tests.txt"
cp "$ROOT/build/host/analysis.txt" "$ROOT/results/reverb_analysis.txt"
sh "$ROOT/tests/bfin_sim/run.sh" "$ROOT/build/sim" | tee "$ROOT/results/bfin_sim.txt"
sh "$ROOT/tools/build.sh" "$ROOT/build/opus" | tee "$ROOT/results/build.txt"
python3 -I "$ROOT/tools/make_release.py" "$ROOT/build/opus"
