# Validation policy

A compressed-size result is accepted only when the decoded bytes match the
original exactly. Decoder size, table size, temporary RAM, stack usage and
decode time are reported separately.

The AMYZ 1.0 release evidence covered:

- 200 files from 15 games, totaling 351,896 raw bytes;
- byte-exact JavaScript decoding of every stream;
- Z80 execution of small, fast and tiny VRAM decoders;
- Z80 execution of small and fast RAM decoders;
- 6502 small, fast and self-modifying decoders;
- TMS9900 RAM and VRAM model execution;
- C and JavaScript identity for fixed tables and selected search profiles;
- direct-to-VRAM access spacing of at least 29 T-states on the Z80 routines.

The 42-picture Graphics II corpus is a separate public comparison contract. A
picture contains exactly 6,144 pattern bytes and 6,144 color bytes. Its existing
results are retained in `benchmarks/graphics-ii`; raw third-party artwork is not
redistributed here by default.

Benchmark claims must state whether they count:

1. compressed payload only;
2. one decoder per program;
3. one 15-byte or 45-byte table per asset group;
4. a private table for every standalone file;
5. a complete runnable executable or ROM.

Without that distinction, shared-table AMYZ numbers are easy to overstate.

