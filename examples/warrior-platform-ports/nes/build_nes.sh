#!/bin/sh
# Build warrior.nes: convert the picture, compress it with AMYZ, assemble and link.
# Needs python3, node, ca65/ld65 (CA65, LD65), and the Warrior files (WARRIOR dir).
set -e
cd "$(dirname "$0")"
CA65=${CA65:-ca65}; LD65=${LD65:-ld65}; mkdir -p data
python3 nes_convert.py "$WARRIOR/pattern.bin" "$WARRIOR/color.bin" data --sprites
node pack_nes.mjs data
$CA65 -D AMYZ_ZP=\$f0 amyz_nes.s -o amyz_nes.o
$CA65 warrior_nes.s -o warrior_nes.o
$LD65 -C nes.cfg -o warrior.nes warrior_nes.o amyz_nes.o
ls -l warrior.nes
