#!/bin/sh
# The ports' compiled Blackfin code in the instruction-set simulator: its output against Hundekraft's original on the
# PC (byte for byte, the same scenarios as tests/host), and its cycles per process call.
#   KENNEL=.../hundekraft-3.1.1-source/ext/kennel OPUS_KIT=.../opus/plugin-kit BFIN_RUN=/path/to/bfin-elf-run \
#       PATH=/path/to/bfin/bin:$PATH sh tests/bfin_sim/run.sh [OUT]
# Compiled with the Opus kit's own flags (tools/op1mod/build.py CFLAGS), as plugin_tool.py build does.
# Copyright (C) 2026 sammi. GPL-3.0-or-later with the OP-1 firmware permission.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
K=${KENNEL:?set KENNEL}/synth_sdk
KIT=${OPUS_KIT:?set OPUS_KIT}
RUN=${BFIN_RUN:?set BFIN_RUN to the simulator (bfin-elf-run)}
B=${1:-$ROOT/build/sim}
T=$ROOT/tests/host
mkdir -p "$B"
F="-mcpu=bf524-any -O2 -ffreestanding -fno-builtin -Wall -Wextra -Werror"     # = the Opus kit's CFLAGS
M="--environment virtual --model bf524 --memory-region 0x0,0x4000000 --memory-region 0xFF800000,0x8000"
bfin-elf-gcc $F -c "$HERE/start.S" -o "$B/start.o"
fail=0
for e in reverb stutter; do
    SRC=$ROOT/plugins/sammi.$e/$e.c
    D=""; [ $e = stutter ] && D=-DTEST_STUTTER
    for mode in dump cycles; do
        C=""; [ $mode = cycles ] && C=-DSIM_CYCLES
        bfin-elf-gcc $F -Wno-unused-function $D $C -DEFFECT_SRC="\"$SRC\"" -I "$KIT/sdk" -I "$T" -c "$HERE/bfin_runner.c" -o "$B/${e}_$mode.o"
        bfin-elf-gcc -mcpu=bf524-any -nostdlib -Wl,--no-warn-rwx-segments -T "$HERE/sim.ld" "$B/start.o" "$B/${e}_$mode.o" -lgcc -o "$B/${e}_$mode.elf"
    done
    "$RUN" $M "$B/${e}_dump.elf" > "$B/bfin_$e.raw"
    "$RUN" $M "$B/${e}_cycles.elf" | tee "$B/cycles_$e.txt"
    # the reference: Hundekraft's original on the PC (tests/host's orig runner, no sanitizers needed here)
    FX=FX_$e; R=""; [ $e = reverb ] && R=-DREVERB
    cc -O2 -w -DOP1_HOST $R -DFX=$FX -DEFFECT_SRC="\"$K/plugins/$e/$e.c\"" -I "$K" -I "$T" "$T/orig_runner.c" -o "$B/orig_$e"
    "$B/orig_$e" > "$B/orig_$e.raw"
    if cmp -s "$B/bfin_$e.raw" "$B/orig_$e.raw"; then
        echo "PASS blackfin $e: the compiled port == Hundekraft's original, $(wc -c < "$B/bfin_$e.raw") bytes"
    else
        echo "FAIL blackfin $e: $(cmp "$B/bfin_$e.raw" "$B/orig_$e.raw" || true)"; fail=1
    fi
done
[ $fail = 0 ] || exit 1
