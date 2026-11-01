#!/bin/sh
# Host tests (the PC, AddressSanitizer + UndefinedBehaviorSanitizer):
#   1. differential: Hundekraft's original reverb.c / stutter.c (unchanged, with op1r's host mocks) against the Opus
#      ports, playing tests/host/scenario.h; the outputs must be identical, byte for byte;
#   2. port_checks: the Opus side alone (registration, limits, needs, the shift layer, the page, shared bulk, sizes);
#   3. analysis.py: what the sound does (decay, tone, wet/dry, stereo; every stutter mode, rate and repeat count).
#
#   KENNEL=/path/hundekraft-3.1.1-source/ext/kennel OPUS_KIT=/path/opus/plugin-kit sh tests/host/run.sh [OUT]
# Copyright (C) 2026 sammi. GPL-3.0-or-later with the OP-1 firmware permission.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
K=${KENNEL:?set KENNEL to hundekraft-3.1.1-source/ext/kennel}/synth_sdk
KIT=${OPUS_KIT:?set OPUS_KIT to the Opus plugin kit}
B=${1:-$ROOT/build/host}
T=$ROOT/tests/host
mkdir -p "$B"
CC=${CC:-cc}
F="-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -Wall -Wextra -Wno-missing-field-initializers -Wno-unused-function"
$CC $F -DOP1_HOST -DREVERB -DFX=FX_reverb -DEFFECT_SRC="\"$K/plugins/reverb/reverb.c\"" -I"$K" -I"$T" "$T/orig_runner.c" -o "$B/orig_reverb"
$CC $F -DOP1_HOST -DFX=FX_stutter -DEFFECT_SRC="\"$K/plugins/stutter/stutter.c\"" -I"$K" -I"$T" "$T/orig_runner.c" -o "$B/orig_stutter"
$CC $F -DEFFECT_SRC="\"$ROOT/plugins/sammi.reverb/reverb.c\"" -I"$KIT/sdk" -I"$T" "$T/port_runner.c" -o "$B/port_reverb"
$CC $F -DSTUTTER_HOST -DEFFECT_SRC="\"$ROOT/plugins/sammi.stutter/stutter.c\"" -I"$KIT/sdk" -I"$T" "$T/port_runner.c" -o "$B/port_stutter"
$CC $F -DEFFECT_SRC="\"$ROOT/plugins/sammi.reverb/reverb.c\"" -DTEST_REVERB -I"$KIT/sdk" -I"$T" "$T/port_checks.c" -o "$B/checks_reverb"
$CC $F -DSTUTTER_HOST -DEFFECT_SRC="\"$ROOT/plugins/sammi.stutter/stutter.c\"" -I"$KIT/sdk" -I"$T" "$T/port_checks.c" -o "$B/checks_stutter"
fail=0
for e in reverb stutter; do
    "$B/orig_$e" > "$B/orig_$e.raw"
    "$B/port_$e" > "$B/port_$e.raw"
    if cmp -s "$B/orig_$e.raw" "$B/port_$e.raw"; then
        echo "PASS differential $e: port == Hundekraft original, $(wc -c < "$B/port_$e.raw") bytes ($(sha256sum < "$B/port_$e.raw" | cut -c1-16))"
    else
        echo "FAIL differential $e: $(cmp "$B/orig_$e.raw" "$B/port_$e.raw" || true)"; fail=1
    fi
done
"$B/checks_reverb" "$B" || fail=1
"$B/checks_stutter" "$B" || fail=1
python3 -I "$T/analysis.py" "$B" || fail=1
[ $fail = 0 ] && echo "host tests: all passed" || { echo "host tests: FAILED"; exit 1; }
