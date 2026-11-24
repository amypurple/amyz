# AMYZ on the Game Boy (Sharp SM83 / LR35902)

Two AMYZ decoders for the Game Boy CPU, a test harness with its own SM83 core,
and a demo ROM that decodes the Warrior picture (converted to 4 greys) into
VRAM with the LCD off and then shows it.

The SM83 is not a Z80: it has no IX/IY, no shadow registers, no ED opcodes
(no LDIR, no DJNZ, no EX). The decoders use `ld a,[hl+]` for the stream and
copies, `ldh` for their HRAM variables, and keep the bit buffer in A.
`swap` did not help here (the bit reader works one bit at a time).

## Decoders

| file | size | table | HRAM | T-states per output byte (corpus) |
|---|---|---|---|---|
| `amyz_gb.asm` (small) | 161 B | 15 B `.tabz` (`amyz_widths`) | 2 B (`amyz_hoff`) | 297.0 |
| `amyzf_gb.asm` (fast) | 204 B | 45 B `.tabzx` (`amyz_bases`) | 3 B (`amyz_hoff`, `amyz_hbits`) | 131.5 |

Sizes are the code only, without the table. The fast one is 2.26 times as fast
for 43 more bytes of code and 30 more bytes of table, so it pays.

- In: HL = compressed stream, DE = destination. Out: DE = end of output.
  Destroys AF, BC, DE, HL. Stack: 8 bytes (small), 4 bytes (fast).
- The destination can be WRAM or HRAM-free RAM, or VRAM **while the LCD is off**
  (matches read back from the destination, and with the LCD on VRAM is not
  accessible during mode 3). Not for in-place decoding.
- Small: same structure as the Z80 `amyz_ram.asm`; the bucket base is computed
  from the 15 widths for each new offset; one shared copy loop and bit reader.
- Fast: the bit read and the gamma code are inlined macros, the bucket base comes
  from the expanded table, the copy loop is split into pages (C counts bytes, B pages).
- The program defines the HRAM addresses (`DEF amyz_hoff EQU $FF80`, and for the
  fast one `DEF amyz_hbits EQU $FF82`) and the table label, then `INCLUDE`s the file
  (rgbds syntax).
- Licence: CC0.

## Corpus test (measured)

`gbtest.c` runs a decoder on one stream in `sm83.h`, an SM83 interpreter
written for this (CC0; documented opcodes, T-state counts from the Pan Docs
tables, no interrupts, no PPU, no memory timing). The decoder sits at $0200,
the table at $3F00, the stream at $4000, the output goes to $C000 (WRAM), HRAM
at $FF80, SP = $FFFE. The output must be byte-exact, DE must point at its end,
the byte after it must be untouched, and any write outside the output, HRAM and
the stack fails the test.

All 200 corpus files (351 896 output bytes) decode byte-exact with both
decoders (`corpus_test.txt`):

    small: ok 200 fail 0  T/byte 297.0
    fast:  ok 200 fail 0  T/byte 131.5

At 4.194 MHz that is about 14.1 KB/s (small) and 31.9 KB/s (fast).

To run it: assemble a harness image (`h_small.asm` / `h_fast.asm` place the
decoder at $0200 and an empty table at $3F00), then the script:

    rgbasm -o h_fast.o h_fast.asm && rgblink -x -o h_fast.bin h_fast.o
    gcc -O2 -o gbtest gbtest.c
    ./run_gb.sh h_fast.bin tabzx STREAMS TABLES CORPUS WGAME

(STREAMS/TABLES: the AMYZ encoder corpus streams and fitted tables; CORPUS and
WGAME: the original files. Not shipped here: they are Amy's game data.)

## Demo ROM: `warrior_gb.gb`

1. `gb_convert.py`: the 256x192 TMS9918A picture is resampled to 160x120 (box
   filter on luminance, so all of it stays on screen), quantised to 4 greys by
   1-D k-means (no dithering, so tiles repeat), cut into 2 bpp tiles, and
   identical tiles are merged: 300 tiles -> **186 unique** (with the black
   border tile). That fits in one 256-tile block, so no split between the two
   tile blocks and no window were needed (the script stops if a picture needs
   more than 256; the split is not implemented). It writes `data/tiles.bin`
   (2976 B), `data/map.bin` (18 rows of the 32-wide map, 576 B) and
   `data/preview.png` (the expected screen).
2. `pack_gb.mjs`: AMYZ with one fitted table for both: tiles 2976 -> 2098 B,
   map 576 -> 238 B; writes `gb.tabz` and `gb.tabzx`; round trip checked.
3. `warrior_gb.asm`: waits for VBlank, turns the LCD off, decodes the tiles to
   $8000 and the map to $9800, sets BGP = %11100100, turns the LCD on
   (LCDC = $91) and loops. 32 KB ROM-only cartridge, `rgbfix -v`: valid Nintendo
   logo, header checksum and global checksum (checked again in Python).
   `warrior_gb.gb` uses the fast decoder, `warrior_gb_small.gb` the small one.
4. `build_gb.sh` does all of it (`RGBDS=<rgbds dir> WARRIOR=<dir with
   pattern.bin, color.bin> ./build_gb.sh`).

Decode time for the picture, from the harness (not from an emulator): fast
611 268 + 40 628 T = about 155 ms; small 1 477 268 + 62 984 T = about 367 ms.

## Emulator test (measured)

Both ROMs were run headless through `../retroshot.c` with two libretro cores
built here from their public git repositories:

- **Gambatte** (github.com/libretro/gambatte-libretro), 120 frames, no boot ROM:
  `shot_warrior_gb_gambatte.png`, `shot_warrior_gb_small_gambatte.png`.
- **SameBoy 1.0.5** (github.com/LIJI32/SameBoy, `make libretro`, with its own
  free boot ROMs built with rgbds), 300 frames, model "Game Boy" (DMG) and
  "Auto": `shot_warrior_gb_sameboy.png`. SameBoy's DMG boot ROM checks the
  header logo and checksum, so this also checks the header.

In all runs the screen matches `data/preview.png` pixel for pixel (4 grey
levels mapped in order; 0 differing pixels), and the fast and small ROMs give
identical screens.

## What was not tested

- **Real hardware:** not tested (no DMG, GBC or flash cartridge here).
- **The SM83 core in `sm83.h`** was not run against CPU test ROMs (Blargg's
  `cpu_instrs` etc.). Its correctness for these decoders is shown by the
  byte-exact results and by the emulator screens; its cycle counts come from
  the Pan Docs tables and were **not** cross-checked against an emulator's
  timing. The T/byte figures above are from this core only.
- **Decoding to VRAM with the LCD on** (in VBlank or by checking STAT): not
  supported by these decoders and not tried.
- **More than 256 unique tiles** (split across the tile blocks, or the window):
  not needed for Warrior, so not implemented or tested.
- **Game Boy Color mode:** the ROM is a DMG ROM (CGB flag $00); SameBoy "Auto"
  runs it, other CGB colourisation was not checked.
- mGBA was not built.

## Files

| path | contents |
|---|---|
| `amyz_gb.asm`, `amyzf_gb.asm` | the decoders |
| `sm83.h`, `gbtest.c`, `run_gb.sh`, `h_small.asm`, `h_fast.asm` | the harness |
| `corpus_test.txt` | the corpus results |
| `gb_convert.py`, `pack_gb.mjs` | picture conversion and packing (`pack_gb.mjs` imports `../lib/amyz.js`) |
| `warrior_gb.asm`, `build_gb.sh` | the demo |
| `data/` | converted and packed picture data, `preview.png`, `stats.txt` |
| `warrior_gb.gb`, `warrior_gb_small.gb` | the demo ROMs |
| `shot_*.png` | emulator screenshots (3x) |

Not shipped: rgbds (MIT), the Gambatte (GPL-2.0) and SameBoy (Expat) cores and
their sources. Tools used: rgbds from github.com/gbdev/rgbds (rgbasm, rgblink,
rgbfix, rgbgfx for SameBoy's boot ROMs).
