#!/bin/sh
# Make a git checkout's generated autotools files newer than their inputs, in dependency order,
# as a release tarball has them, so make does not try to regenerate them.
cd "$1" || exit 1
t=200001010000
for pat in 'configure.ac' 'configure.in' '*.m4' 'Makefile.am' '*.def' '*.y' '*.l' ; do find . -name "$pat" -not -path './.git/*' -exec touch -t $t {} + ; done
t=200101010000; find . -name aclocal.m4 -not -path './.git/*' -exec touch -t $t {} +
t=200201010000; find . \( -name configure -o -name 'config.in' -o -name 'config.h.in' -o -name 'config-h.in' \) -not -path './.git/*' -exec touch -t $t {} +
t=200301010000; find . -name Makefile.in -not -path './.git/*' -exec touch -t $t {} +
t=200401010000; find . \( -name '*.info' -o -name 'mparam_h.in' \) -not -path './.git/*' -exec touch -t $t {} +
