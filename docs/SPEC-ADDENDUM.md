# AMYZ SPEC addendum (sections 9 and 10)

These sections are meant to be appended to `formats/amyz/SPEC.md`. They do not change the format,
and they do not change any existing decoder.

## 9. Reverse streams (backward decoding)

A reverse stream is the same AMYZ grammar read backwards. It is not a new format:

```
reverse_stream(data) = reverse_bytes( AMYZ( reverse_bytes(data) ) )
```

Here `AMYZ()` is the normal encoder, with the same table or tiny mode. A backward decoder works
as follows:

- It reads stream bytes from the last one down to the first. Each byte is either a bit byte or a
  raw byte, exactly as in section 2. Bits within a bit byte are still read MSB first.
- It writes output bytes from the end of the destination down to its start.
- It copies a match from `dst + offset`, where `dst` is the address of the byte about to be
  written, instead of `dst - offset`. Copies run byte by byte, downward, so an overlapping copy
  (n > offset) repeats a pattern exactly as in section 3.
- The grammar, gamma codes, buckets, tables, the end marker and the initial last offset (1) are
  unchanged.

Any AMYZ encoder therefore produces reverse streams. To make one, reverse the input, encode it,
then reverse the bytes of the result. The reference backward 6502 decoder is
`amyzpack4/depackb.s`.

**Why it exists: in-place decrunching without moving the data.** A packed C64 program loads
just above its destination. Decoding backward from the top, the writer starts above the reader
and stays above the unread data for most of the run. Near the end it can catch up, because the
BASIC line sits below the compressed data. amyzpack4 replays the decoding to find the exact
number m of lowest output bytes for which this would happen. It stores those m bytes uncompressed
in a small side buffer (the C64 tape buffer, `$0334`) and copies them to their place after
decrunching. This is the "safety margin" that in-place packers need: dali and Exomizer handle it
with a distance between the data and its destination.

## 10. Speed-aware parsing (`-speed N`)

The optimal parser (section 6) minimises bits. With `-speed N`, it minimises this instead:

```
cost = 1000 * bits + N * estimated decoder cycles
```

The cycle estimate for a parse is linear in its token statistics:

| term | meaning |
|---|---|
| `bit` | flag and gamma bits |
| `offBit` | bucket index and extra bits |
| `litTok`, `litByte` | literal runs and literal bytes |
| `repTok` | repeat-offset tokens |
| `newTok` | new-offset tokens |
| `matchByte` | bytes copied by matches |

The coefficients are measured, not guessed. `speed/fit_model.mjs` decodes all 200 corpus streams
with the real decoder in its emulator and fits the cycle counts by least squares:

| decoder | bit | offBit | litTok | litByte | repTok | newTok | matchByte | fit error |
|---|---|---|---|---|---|---|---|---|
| 6502 `amyzf_6502` | 16.2 | 21.3 | 72.4 | 16.7 | 64.5 | 114.3 | 17.0 | 0.28% mean, 1.9% worst file |
| Z80 `amyzf_vram` | 33.9 | 49.8 | 112.4 | 33.9 | 170.5 | 407.3 | 115.1 | 0.02% mean, 0.6% worst file |

How the cost is used:

- Each term enters the parser where the token is chosen, so the dynamic programming stays exact
  for the combined cost.
- `-speed 0` is exactly the size-optimal parse: the default model has bit = 1, literal byte = 8,
  and no other terms.
- The table is still fitted for size, and the stream is decoded by the unchanged decoders.
- JS: `setCostModel()` in `amyz_core.js`.
- C: the `CM_*` costs in `amyzc_core.c`, set by `AMYZ_SPEED=N AMYZ_CPU=6502|z80` in `encode`
  mode.
- The C and JS parsers give identical streams: 200/200 files at N = 0 and at N = 16.
