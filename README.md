# AMYZ

AMYZ is an open compression format for 8-bit systems. It combines an optimal
LZ parser with a compact implicit-state stream and offset buckets fitted to a
group of assets. Its primary target is games that keep several graphics or
level resources in ROM and decode them with a small routine.

The repository includes reference encoders in JavaScript, Python and C,
decoders for Z80, 6502 and TMS9900, direct-to-TMS9918A-VRAM Z80 and TMS9900
decoders, and a Commodore 64 self-extracting packer.

## One format, several tradeoffs

AMYZ has one token grammar and one stream format. The options below do not
create unrelated formats:

| Profile | Table | Intended use |
|---|---:|---|
| Small/shared | 15 bytes per asset group | Best general ROM-size result when several files share a table |
| Fast | Expanded 45-byte form of the same table | Same streams, faster decoder |
| Tiny | Fixed table, no stored table | A single small resource or very small program |
| Reverse | Same grammar read backwards | In-place backward decompression |

An encoder's search effort also does not change the format. The JavaScript,
Python and C encoders may find different fitted tables when given different
search budgets, but compatible decoders consume the resulting streams.

An experimental alternative token format called AMYZ2 was measured and
rejected. It is not a successor to AMYZ and is not part of the public format or
packer interface. See [`docs/REJECTED-EXPERIMENTS.md`](docs/REJECTED-EXPERIMENTS.md).

## Why AMYZ exists

AMYZ continues Amy Bienvenu's DAN1-DAN3 compression experiments. It borrows
documented ideas from [ZX0](https://github.com/einar-saukas/ZX0) by Einar
Saukas: optimal parsing, an implicit-state grammar, repeat-last-offset tokens
and interlaced Elias gamma coding. Its fitted offset buckets were inspired by
Exomizer by Magnus Lind. AMYZ is a new format with its own specification,
encoders, tables and decoders.

The design goal is not to win every compression test. It is to provide a useful
size/speed/code-size balance on 8-bit machines, especially when a game can share
one fitted table across many assets.

## Measured results

On the original 15-game, 200-file corpus (351,896 raw bytes), the measured net
cost was:

| Codec/profile | Compressed data | Decoder + table | Net total | Relative to ZX0 |
|---|---:|---:|---:|---:|
| ZX0 | 83,241 | 135 | 83,376 | baseline |
| AMYZ small/shared | 79,912 | 197 | **80,109** | **-3.9%** |
| AMYZ fast | 80,626 | 248 | **80,874** | **-3.0%** |
| AMYZ tiny | 83,126 | 152 | 83,278 | -0.1% |

These totals count one decoder and one table per game. They must not be quoted
as the cost of independent files that each carry a private table. Per-game
results and measurement qualifications are in
[`benchmarks/game-corpus-results.md`](benchmarks/game-corpus-results.md).

The public Graphics II corpus contains 42 unique 256x192 pictures, each stored
as one 6,144-byte pattern table plus one 6,144-byte color table. AMYZ produced
161,148 payload bytes, compared with 161,374 for Exomizer 2 and 166,556 for
ZX0. Counting one private 15-byte table for every picture raises AMYZ to
161,778 bytes before decoder code. It beat ZX0 on all 42 payloads and on 36 of
42 first-use totals. Full results and the reproducible runner are under
[`benchmarks/graphics-ii`](benchmarks/graphics-ii/README.md).

## Decoder choices

| CPU / destination | Small | Fast | Tiny | Notes |
|---|---:|---:|---:|---|
| Z80 to TMS9918A VRAM | 182 B, 345.2 T/B | 203 B, 206.3 T/B | 152 B, 246.3 T/B | At least 29 T between VDP data accesses |
| Z80 to RAM | 124 B, 212.3 T/B | 134 B, 103.8 T/B | - | Stack only; no IX/IY |
| 6502 to RAM | 274 B, 119.5 cycles/B | 488 B, 54.3 cycles/B | - | 11 zero-page bytes |
| TMS9900 to VRAM | 290 B + vector, 642.0 cycles/B | - | - | Includes modeled TI wait states |
| TMS9900 to RAM | 228 B + vector, 349.6 cycles/B | - | - | Includes modeled TI wait states |

All cycle figures are corpus averages from the supplied harnesses. They are not
guarantees for every stream or machine configuration.

## Quick start

Tiny mode needs no fitted table:

```js
import { compressTiny, decompressTiny } from "./encoders/js/amyz.js";

const packed = compressTiny(input);
const restored = decompressTiny(packed);
```

For several related files, fit one shared table:

```js
import { fitGroupAMYZ, decompressTable } from "./encoders/js/amyz.js";

const { table, streams } = fitGroupAMYZ([patterns, colors, tilemap]);
const restored = decompressTable(streams[0], table);
```

Z80, shared-table VRAM decoder:

```asm
amyz_widths:
        incbin "game.tabz"       ; 15 bytes, once per game

        ld hl,packed_data
        ld de,$0000               ; destination VRAM address
        call amyz_decompress
```

See [`SPEC.md`](SPEC.md) for the normative stream definition and
[`docs/PORTS.md`](docs/PORTS.md) for calling conventions.

For a no-install workflow, open the self-contained
[`AMYZ Web Packer`](tools/amyz-web-packer.html). It runs locally in the browser,
checks every packed stream by decoding it again, and can export the matching
decoder source. The later
[`machine comparison`](benchmarks/machine-comparison.md) documents measured
results on C64, Z80/TMS9918A systems, TI-99/4A, NES and Game Boy.

The self-contained bilingual
[`Amy Image Converter`](tools/amyz-image-converter.html) prepares raw image data
for the packer. It supports TMS9918A Graphics II, C64 hires and multicolor,
Game Boy 2bpp and NES tiles/attributes, with local previews and downloads. The
TMS9918A path can also export Pletter data and a ready-to-use CVBasic snippet.

## Repository layout

- `encoders/`: JavaScript, readable Python reference, and optimized C encoder.
- `decoders/`: Z80, 6502 and TMS9900 implementations.
- `tools/amyzpack/`: C64 self-extracting packer and forward/backward depackers.
- [`tools/amyz-web-packer.html`](tools/amyz-web-packer.html): self-contained bilingual browser packer.
- [`tools/amyz-image-converter.html`](tools/amyz-image-converter.html): offline multi-platform image converter.
- `benchmarks/`: measured results and the Graphics II comparison contract.
- `examples/warrior-platform-ports/`: validated MSX1, SG-1000, NES and Game Boy ports,
  including reproducible builds, ROMs, screenshots and decoder evidence.
- `tests/`: portable smoke tests and licensed CPU validation harnesses.
- `SPEC.md`: normative format specification.

## Validation

Run the dependency-free JavaScript smoke test with Node.js 18 or later:

```text
npm test
```

The release evidence also includes byte-exact decoder runs over 200 files. The
Z80 VRAM implementation was integrated in Amy Studio and validated against
exact VRAM output. See [`docs/VALIDATION.md`](docs/VALIDATION.md).

## License

AMYZ source code and specification are released under the
[BSD 3-Clause License](LICENSE). Third-party projects that inspired the design
remain under their own licenses; no license in this repository supersedes
theirs. See [`NOTICE.md`](NOTICE.md).

