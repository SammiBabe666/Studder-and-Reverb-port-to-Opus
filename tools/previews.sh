#!/bin/sh
# The pages' previews: the plugins' own drawing and audio code on the PC (tests/host/preview.c), frames turned into PNG
# and GIF files (tests/host/preview.py, needs Pillow) -> docs/media/.
#   OPUS_KIT=/path/to/opus/plugin-kit sh tools/previews.sh
# Copyright (C) 2026 sammi. GPL-3.0-or-later with the OP-1 firmware permission.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
KIT=${OPUS_KIT:?set OPUS_KIT to the Opus plugin kit}
B=$ROOT/build/preview
T=$ROOT/tests/host
mkdir -p "$B"
CC=${CC:-cc}
F="-O2 -Wall -Wextra -Wno-unused-function"
$CC $F -DTEST_REVERB -DEFFECT_SRC="\"$ROOT/plugins/sammi.reverb/reverb.c\"" -I"$KIT/sdk" -I"$T" "$T/preview.c" -o "$B/preview_reverb"
$CC $F -DSTUTTER_HOST -DEFFECT_SRC="\"$ROOT/plugins/sammi.stutter/stutter.c\"" -I"$KIT/sdk" -I"$T" "$T/preview.c" -o "$B/preview_stutter"
"$B/preview_reverb" "$B"
"$B/preview_stutter" "$B"
python3 -I "$T/preview.py" "$B" "$ROOT/docs/media"
