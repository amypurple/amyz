#!/bin/sh
# Build warrior_gb.gb (fast decoder) and warrior_gb_small.gb (small decoder):
# convert the picture, compress it with AMYZ, assemble, link, fix the header.
# Needs python3 + Pillow, node, rgbds (RGBDS dir with rgbasm/rgblink/rgbfix),
# and the Warrior files (WARRIOR dir with pattern.bin and color.bin).
set -e
cd "$(dirname "$0")"
R=${RGBDS:-}; [ -n "$R" ] && R="$R/"
python3 gb_convert.py "$WARRIOR/pattern.bin" "$WARRIOR/color.bin" data
node pack_gb.mjs data
for v in fast small; do
  o=warrior_gb.gb; d=""; [ $v = small ] && { o=warrior_gb_small.gb; d="-DSMALL=1"; }
  ${R}rgbasm $d -o w_$v.o warrior_gb.asm
  ${R}rgblink -p 0 -o $o w_$v.o
  ${R}rgbfix -v -p 0 -t "AMYZ WARRIOR" $o
  rm -f w_$v.o
done
ls -l warrior_gb.gb warrior_gb_small.gb
