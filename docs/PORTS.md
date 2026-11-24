# Implementations and calling conventions

## Z80

Every forward decoder enters with `HL` pointing to the stream and `DE` holding
the destination address. The VRAM variants target TMS9918A port `$BE`/`$BF` and
maintain at least 29 T-states between data-port accesses.

- `amyz_vram.asm`: shared 15-byte table at `amyz_widths`.
- `amyzf_vram.asm`: expanded 45-byte table at `amyzf_bases`.
- `amyzt_vram.asm`: fixed tiny table, no external table.
- `amyz_ram.asm` and `amyzf_ram.asm`: RAM destinations, not in-place.

The routines use at most six stack bytes beyond the return address and do not
use IX or IY.

## 6502

The caller stores source and destination pointers in the exported AMYZ zero-page
area and calls `amyz_decompress`, `amyzf_decompress`, or `amyzs_decompress`.
The default zero-page base is `$F0` and can be moved at assembly time.

The small and fast variants are ROM-safe. The `amyzs` variant is
self-modifying and must execute from RAM. None decode in place; the C64 packer
provides a safe layout around that restriction.

## TMS9900

The TMS9900 ports use `BLWP`, with `R0` as the source and `R1` as the VRAM or
RAM destination. They reserve a 32-byte workspace and return the first address
after the output in `R1`.

The supplied timing includes both zero-wait-state estimates and modeled
TI-99/4A memory wait states. Byte-exact behavior is stronger evidence than the
cycle model, which still warrants confirmation on hardware or a mature emulator.

## Game Boy SM83

The caller enters with `HL` pointing to the compressed stream and `DE` holding
the destination address. The destination may be WRAM, or VRAM while the LCD is
off. The small decoder uses two HRAM bytes and an external 15-byte table; the
fast decoder uses three HRAM bytes and an expanded 45-byte table. Both return
the first address after the output in `DE` and do not decode in place.

## C64 packer

`tools/amyzpack/amyzpack4.mjs` creates a self-extracting PRG. It can select
forward or backward in-place decompression, choose tiny or fitted-table mode,
and trade compressed size for decode time. Run it without arguments for the
current command-line syntax.

