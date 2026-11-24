# History

AMYZ grew through a sequence of experimental formats during November 2026:

1. **ZXB** explored a ZX0-like grammar with different coding tradeoffs.
2. **AMY1** introduced game-fitted offset buckets.
3. **AMY2** reduced decoder and stream costs and added a table-free tiny mode.
4. **AMYZ 1.0** stabilized the grammar, 15-byte shared table, expanded fast
   table, reference encoders and cross-CPU decoders.

Later work added a C encoder with a deeper table search, speed-aware parsing,
6502 and TMS9900 ports, and forward/backward C64 self-extracting packers. These
change encoder strategy or stream direction, not the forward AMYZ grammar.

The format continues Amy Bienvenu's DAN1-DAN3 experiments, but AMYZ is not a
DAN stream and requires its own decoder.

