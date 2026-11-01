#!/bin/sh
# Build the Blackfin instruction-set simulator (binutils-gdb sim/bfin) from the binutils tree build_toolchain.sh
# fetched, for tests/bfin_sim:
#   sh tools/toolchain/build_sim.sh WORK_DIR        (the same WORK_DIR as build_toolchain.sh)
#   -> WORK_DIR/build/sim/sim/bfin/run   (pass it as BFIN_RUN)
# Copyright (C) 2026 sammi. GPL-3.0-or-later. binutils-gdb keeps its own licence.
set -eu
W=${1:?WORK_DIR}
export PATH="$W/host/bin:$PATH"
mkdir -p "$W/build/sim"; cd "$W/build/sim"
"$W/src/binutils/configure" --target=bfin-elf --prefix="$W/sim" --disable-nls --disable-werror --disable-gdb \
    --disable-gdbserver --enable-sim --disable-gprofng --disable-ld --disable-gas --disable-binutils \
    --disable-libdecnumber --disable-readline --without-zstd --without-debuginfod MAKEINFO=true
make -j2 all-sim MAKEINFO=true
echo "simulator: $W/build/sim/sim/bfin/run"
