#!/bin/sh
# Build the Blackfin cross toolchain (bfin-elf: GCC 13.3.0 C compiler + libgcc, binutils 2.42) that the kennel packer
# needs, from git sources only (for machines that reach GitHub but not ftp.gnu.org or a package manager).
#
#   sh toolchain/build_toolchain.sh [WORK_DIR]      -> WORK_DIR/bfin/bin/bfin-elf-{gcc,ld,as,...}
#
# If you can use ordinary GNU release tarballs, the SDK's own recipe is simpler (op1r-src toolchain/Dockerfile:
# binutils + GCC --target=bfin-elf). This script exists because building from git needs flex (git trees do not carry
# the generated lexers) and a newer MPFR than reachable here. What it does:
#   1. flex 2.6.4 from FreeBSD's tree (contrib/flex + usr.bin/lex's pre-generated bootstrap scanner, parser, skeleton),
#      built twice (stage 2 regenerates and must reproduce its own scanner)
#   2. GMP 6.2.1 and MPFR 3.1.3 (configure trees from GitHub mirrors), static
#   3. MPC 1.4.2dev compiled by hand (its git tree has no configure script) plus mpfr4_compat.c: three MPFR 4 functions
#      MPC uses (mpfr_cmpabs_ui, mpfr_fmma, mpfr_fmms), correctly rounded via exact products
#   4. binutils 2.42 (upstream tag, from a GitHub fork that carries it) and GCC 13.3.0 (gcc-mirror), timestamps fixed so
#      make does not try to regenerate autotools output
# Needs: a host C/C++ compiler, make, m4, bison, git, autoconf is not needed. ~2 GB of disk, ~1 hour on 2 cores.
# Copyright (C) 2026 the hollow_saint authors. GPL-3.0-or-later. The fetched projects keep their own licences.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
W=${1:-$PWD/bfin-toolchain}
mkdir -p "$W/src" "$W/build" "$W/host/bin" "$W/host/lib" "$W/host/include"
P=$W/host
cd "$W/src"
clone() { [ -d "$2" ] || git clone -q --depth 1 ${3:+--branch "$3"} "https://github.com/$1" "$2"; }
clone espressif/binutils-gdb binutils binutils-2_42
clone gcc-mirror/gcc gcc releases/gcc-13.3.0
clone alisw/GMP gmp v6.2.1
clone alisw/MPFR mpfr v3.1.3
clone BrianGladman/mpc mpc
if [ ! -d freebsd ]; then
    git clone -q --depth 1 --filter=blob:none --sparse https://github.com/freebsd/freebsd-src freebsd
    (cd freebsd && git sparse-checkout set usr.bin/lex contrib/flex)
fi

# 1. flex
B=$W/build/flex; mkdir -p "$B"; cd "$B"
cp "$W"/src/freebsd/contrib/flex/src/*.[chly] "$W"/src/freebsd/contrib/flex/src/flex.skl .
cp "$W/src/freebsd/usr.bin/lex/config.h" .
cp "$W/src/freebsd/usr.bin/lex/initscan.c" scan.c
cp "$W/src/freebsd/usr.bin/lex/initparse.c" parse.c
cp "$W/src/freebsd/usr.bin/lex/initparse.h" parse.h
cp "$W/src/freebsd/usr.bin/lex/initskel.c" skel.c
SRCS="buf.c ccl.c dfa.c ecs.c filter.c gen.c main.c misc.c nfa.c options.c parse.c regex.c scan.c scanflags.c scanopt.c skel.c sym.c tables.c tables_shared.c tblcmp.c yylex.c"
gcc -O2 -w -DHAVE_CONFIG_H -I. -o stage1flex $SRCS -lm
./stage1flex -o scan.c scan.l
gcc -O2 -w -DHAVE_CONFIG_H -I. -o flex $SRCS -lm
./flex -o scan_check.c scan.l
sed 's/scan_check\.c/scan.c/g' scan_check.c | cmp - scan.c
gcc -O2 -c libmain.c libyywrap.c && ar rcs libfl.a libmain.o libyywrap.o
cp flex "$P/bin/"; ln -sf flex "$P/bin/lex"; cp libfl.a "$P/lib/"
export PATH="$W/bfin/bin:$P/bin:$PATH"

# 2. GMP, MPFR
for lib in gmp mpfr; do
    sh "$HERE/touch_generated.sh" "$W/src/$lib"
    mkdir -p "$W/build/$lib"; cd "$W/build/$lib"
    extra=""; [ $lib = mpfr ] && extra="--with-gmp=$P"
    "$W/src/$lib/configure" --prefix="$P" --disable-shared --enable-static $extra MAKEINFO=true
    make -j2 MAKEINFO=true && make install MAKEINFO=true
done

# 3. MPC by hand
B=$W/build/mpc; mkdir -p "$B"; cd "$B"
S=$W/src/mpc/src
sed 's/@MPC_HAVE_COMPLEX_H@/0/' "$S/mpc.h.in" > mpc.h
sed 's/@MPC_HAVE_COMPLEX_H@/0/' "$S/mpc-impl.h.in" > mpc-impl.h
printf '#define STDC_HEADERS 1\n#define HAVE_STDLIB_H 1\n#define HAVE_INTTYPES_H 1\n#define HAVE_STDINT_H 1\n#define HAVE_LOCALECONV 1\n#define HAVE_INTMAX_T 1\n' > config.h
for f in "$S"/*.c; do gcc -O2 -w -DHAVE_CONFIG_H -I. -I"$S" -I"$P/include" -c "$f" -o "$(basename "$f" .c).o"; done
gcc -O2 -Wall -I"$P/include" -c "$HERE/mpfr4_compat.c" -o mpfr4_compat.o
rm -f libmpc.a; ar rcs libmpc.a ./*.o
cp libmpc.a "$P/lib/"; cp mpc.h "$P/include/"

# 4. binutils, GCC
sh "$HERE/touch_generated.sh" "$W/src/binutils"
mkdir -p "$W/build/binutils"; cd "$W/build/binutils"
"$W/src/binutils/configure" --target=bfin-elf --prefix="$W/bfin" --disable-nls --disable-werror --disable-gdb \
    --disable-gdbserver --disable-sim --disable-gprofng --disable-libdecnumber --disable-readline --without-zstd \
    --without-debuginfod MAKEINFO=true
make -j2 MAKEINFO=true && make install MAKEINFO=true
(cd "$W/src/gcc" && ./contrib/gcc_update --touch >/dev/null)
mkdir -p "$W/build/gcc"; cd "$W/build/gcc"
"$W/src/gcc/configure" --target=bfin-elf --prefix="$W/bfin" --enable-languages=c --without-headers --with-newlib \
    --disable-nls --disable-libssp --disable-libquadmath --disable-libgomp --disable-shared --disable-threads \
    --with-gmp="$P" --with-mpfr="$P" --with-mpc="$P" MAKEINFO=true
make -j2 all-gcc MAKEINFO=true
make -j2 all-target-libgcc MAKEINFO=true
make install-gcc install-target-libgcc MAKEINFO=true
"$W/bfin/bin/bfin-elf-gcc" --version | head -1
echo "toolchain: $W/bfin/bin (put it on PATH for tools/kennel/pack.py)"
