# AMYZ ports: MSX1, SG-1000, NES and Game Boy

AMYZ is by Amy (AmyPurple), made with Claude. It borrows ideas from ZX0 by Einar Saukas and
Exomizer by Magnus Lind.

All three machines read the same `.amyz` streams and `.tabz`/`.tabzx` tables as the ColecoVision
decoders. All figures are MEASURED unless marked otherwise.

## MSX1 and SG-1000 (Z80, TMS9918A VDP, straight to VRAM)

`z80/amyz_vram_port.asm` and `z80/amyzf_vram_port.asm` are the ColecoVision decoders
`amyz_vram` (small) and `amyzf_vram` (fast). The logic is unchanged; only the VDP ports become
equates:

| machine | AMYZ_VDP_DATA | AMYZ_VDP_CTRL |
|---|---|---|
| ColecoVision | $BE | $BF |
| SG-1000 / SC-3000 | $BE | $BF |
| MSX | $98 | $99 |

Calling convention: HL = stream, DE = VRAM address, and the table at `amyz_widths` (small) or
`amyzf_bases` (fast). Interrupts must be off during the call, as on the ColecoVision. RAM used:
stack only.

### Corpus test

All 200 files, on the toolkit's Z80 + TMS9918A harness (`z80/vtest_port.c`, with the port base
selectable):

| decoder | code | MSX ports | SG-1000 ports | T/byte | smallest gap between VRAM data accesses |
|---|---|---|---|---|---|
| amyzf_vram (fast) | 203 B | 200/200 | 200/200 | 206.1 | 34 T (0 under 29) |
| amyz_vram (small) | 182 B | 200/200 | 200/200 | 351.3 | 34 T (0 under 29) |

The T counts use plain Z80 timing. An MSX1 adds one wait state per M1 cycle. That makes the
real T/byte higher (not measured) and every gap only longer, so the 29 T rule still holds.

### Emulator test

The test ROM is `warrior/warrior_z80.asm`:
- MSX: 16 KB cartridge at $4000 with an "AB" header (`warrior_msx.rom`);
- SG-1000: 16 KB cartridge at $0000 (`warrior_sg1000.sg`).

It sets Graphics II, decompresses the Warrior pattern and colour tables (the 15-game corpus
streams) to VRAM with `amyzf_vram`, and turns the display on.

The emulator is blueMSX, as the libretro core built from github.com/libretro/blueMSX-libretro.
It is driven headless by `retroshot.c`, a minimal libretro frontend written for this, which runs
N frames and saves the last one:

- **MSX:** machine "MSX - C-BIOS" (the free C-BIOS shipped with the core), 600 frames:
  `shot_msx.png`.
- **SG-1000:** machine "SEGA - SG-1000", 300 frames: `shot_sg.png`. The core's mapper
  auto-detection does not know this homebrew ROM, which gave a black screen, so the mapper is
  set to `SG1000` (`bluemsx_cartmapper=SG1000`). That is a core setting, not a decoder issue.

Both screenshots show the full picture, identical to the ColecoVision.

## NES (6502, writing through PPU $2006/$2007)

`nes/amyz_nes.s` is the small 6502 decoder (`amyz_6502.s`) with its copies replaced:
- **literal runs:** set the PPU address once, then write the bytes to $2007;
- **matches:** read back from the PPU, one byte at a time. Each byte sets the source address,
  reads $2007 twice (the first read is the stale read buffer), sets the destination address and
  writes $2007.

Call it with rendering off (or in vblank) and PPUCTRL increment 1: `amyz_src` = stream,
`amyz_dst` = PPU address ($0000-$3EFF). It uses 11 zero-page bytes and no RAM tables. Code:
303 B, plus the 15-byte table.

### Corpus test

The harness is `nes/runnes.c`: the 6502 core with a PPU model (address latch, $2007 read
buffer, 16 KB PPU memory). Result: **200/200 byte-exact, 183.2 cycles per output byte**, which
is about 9.8 KB/s at 1.79 MHz. The harness also checks that no CPU RAM is written outside the
zero page and the stack.

### Emulator test (adapted port)

`nes/warrior.nes` (NROM-128, 8 KB CHR-RAM; build with `nes/build_nes.sh`) runs on FCEUmm, the
libretro core built from github.com/libretro/libretro-fceumm, through `retroshot`.
Screenshot: `shot_nes.png`.

The picture had to be adapted (`nes/nes_convert.py`), because the NES background differs from
the TMS9918A:
- NES tiles are 2-bit, coloured with 4 palettes of 3 colours chosen per 16x16 area, where the
  TMS9918A has 2 colours per 8x1 line;
- only 256 tiles fit in one pattern table.

The conversion:

1. **Aligns the picture to the attribute grid.** The picture starts 2 tile rows down (16 px),
   so each NES 16x16 attribute area lines up with the source picture.
2. **Palettes chosen by optimisation.** The 4 background palettes minimise the weighted number
   of pixels that must be recoloured. Hair reds weigh 20, skin and light yellows 12, white 4,
   and everything inside the face and hair box (x 100-168, y 64-112) 4 times more. The search
   starts greedy, then runs a local search over all 120 colour triples until no swap helps.
   Pixels outside their area's palette get the nearest palette colour.
   - Result: **460 of 49152 pixels recoloured (0.94%)**, 46 of them in the face and hair box.
   - Palettes: (light blue, red, light yellow), (cyan, white, light yellow),
     (dark red, red, cyan), (yellow, white, light yellow).
   - The TMS dark blue of the arch maps to NES $12.
3. **Sprite overlay on the face.** 3 sprites with their own palette ($22, $2C) restore the
   recoloured pixels in the face and hair box: **0 recoloured pixels remain there** (405
   elsewhere).
   - Limits are checked in the script: at most 8 sprites per scanline and 64 in total.
     Sprite 0 is included, so there is no flicker.
   - The sprite tiles live in pattern table 0 next to the background tiles (175 of 256 used).
4. **Split marker moved to the last line.** Sprite 0 is now a 1-pixel tile that hits on the
   last line of the top half, at x = 206, over an opaque background pixel, and is drawn behind
   the background.
5. **Start-up order.** Palettes (32 B), CHR-RAM and nametable decoding (`amyz_nes`), then the
   OAM image (`oam.bin`, OAM DMA), all with rendering off. Rendering is turned on last, in
   vblank.

The converted data is CHR 8192 → 2777 B and nametable + attributes 1024 → 429 B. It decodes
byte-exact through `amyz_nes` in the PPU harness (`nes/data/decode_test.txt`).

`shot_nes.png` was captured in FCEUmm after 120 frames.

The face, the skin and the red hair are back. The remaining differences are elsewhere: the
flowers lose their magenta, and the shield emblem turns cyan.

## Game Boy (Sharp SM83 / LR35902)

`gb/amyz_gb.asm` and `gb/amyzf_gb.asm` are native SM83 decoders rather than
Z80 translations. They use `ld a,[hl+]`, keep their small state in HRAM and
decode to WRAM or to VRAM while the LCD is off.

| decoder | code | table | measured corpus speed |
|---|---:|---:|---:|
| small | 161 B | 15 B | 297.0 T-states/output byte |
| fast | 204 B | 45 B | 131.5 T-states/output byte |

Both pass all **200/200** corpus files byte-exact. The fast decoder is 2.26
times as fast for 73 additional bytes including its expanded table.

The 32 KB `gb/warrior_gb.gb` demonstration converts the full Warrior picture
to four greys, merges its 300 source tiles into 186 unique tiles, and decodes
the tile and map data into VRAM with the LCD disabled. Gambatte and SameBoy
produce the same pixels as `gb/data/preview.png`; the valid Nintendo logo and
header checksum also pass SameBoy's DMG boot ROM. See `gb/README.md` for the
calling convention, build instructions, measurements and limitations.

## What could not be tested

- **Real hardware:** not tested on any of the three machines.
- **openMSX and other emulators:**
  - openMSX was not built (it needs SDL2 development files, not available here). blueMSX was
    used instead, through its libretro core.
  - Gearsystem was cloned but not built. The SG-1000 test relies on blueMSX alone.
  - Mesen and FCEUX standalone were not built. FCEUmm (the libretro build of FCEU) was used.
- **MSX1 timing with M1 wait states:** not measured in the cycle harness. blueMSX emulates them,
  and the picture decodes correctly there.
- **NES:**
  - only the small decoder was ported; there is no fast NES decoder;
  - decoding during rendering was not tried: the decoder needs rendering off or vblank;
  - only NROM with CHR-RAM was tested, no other mappers.
- **Sound, input and other system features:** not involved.

## Files

| path | contents |
|---|---|
| `z80/` | port-parameterised decoders, test wrappers and binaries, `vtest_port.c`, `run_port.sh`, `corpus_test.txt` |
| `warrior/` | MSX and SG-1000 demo source (`warrior_z80.asm`), the two ROMs, the Warrior streams and tables |
| `nes/` | `amyz_nes.s`, `runnes.c` and `cpu6502io.h` (harness), `corpus_test.txt`, `nes_convert.py`, the packer script, `warrior_nes.s`, `nes.cfg`, `build_nes.sh`, `data/`, `warrior.nes`, `orig.png` |
| `gb/` | small and fast SM83 decoders, CPU harness, converter, build scripts, two demo ROMs, packed data and Gambatte/SameBoy screenshots |
| `lib/` | the AMYZ JS codec |
| `retroshot.c` | the headless libretro frontend (CC0); build with `gcc -I<libretro-common>/include -o retroshot retroshot.c -ldl` |
| `shot_*.png` | emulator screenshots |

Not shipped: the libretro cores and their sources. blueMSX is under its own licence (see the
blueMSX-libretro repository), and FCEUmm is GPL-2.0.
