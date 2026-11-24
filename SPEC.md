# AMYZ format specification

Version 1.0 (2026-11-23). File extension `.amyz`.

AMYZ is an LZ77-family compression format for 8-bit machines, designed for fast, small Z80
decoders that write either to TMS9918A VRAM (ColecoVision, SG-1000, MSX1) or to RAM.

**Credits.** Format by Amy (AmyPurple), continuing her DAN1-DAN3 line, made with Claude.
Ideas borrowed from ZX0 by Einar Saukas (optimal parser, implicit-state grammar with no flag
after a literal run, repeat-last-offset token, interlaced Elias gamma lengths) and from Exomizer
by Magnus Lind (adaptive offset buckets fitted to the data in alternating passes).

## 1. Overview

A compressed file is a sequence of three token kinds:

| token | meaning |
|---|---|
| LIT | copy `n` raw bytes from the stream (n >= 1) |
| REP | copy `n` bytes from the last offset used (n >= 1) |
| NEW | copy `n` bytes from a new offset (n >= 2), the offset becomes the last offset |

Offsets are coded with a **bucket table**: a 4-bit bucket index `k`, then `widths[k]` extra bits.
The table has 15 buckets (`k` = 0..14); index 15 is the end marker. The table is not in the
stream: it is a separate 15-byte object, usually one per game, shared by all of its files.
**Tiny mode** uses a fixed table and needs no table bytes at all.

## 2. Bit stream

The stream is a sequence of bytes of two kinds, interleaved:

- **bit bytes**, read MSB first (bit 7 first);
- **raw bytes** (literal data), read whole.

The decoder keeps a bit buffer. When it needs a bit and the buffer is empty, it reads the next
byte of the stream as a new bit byte. Raw bytes are read from the stream at the moment the
grammar needs them. So a bit byte sits in the stream exactly where its first bit is needed;
it may be followed by raw bytes that are read before its remaining bits. (This is ZX0's
scheme.) The encoder mirrors it: when it writes the first bit of a new bit byte, it reserves
the byte at the current end of the output and fills its bits later.

The final bit byte is padded with zero bits after the end marker.

### 2.1 Interlaced Elias gamma

Every length is an Elias gamma code with the data bits interlaced with the continue flags.
For a value `v >= 1` with binary form `1 b(m-1) ... b0` (m bits after the leading 1):

```
for i = m-1 down to 0:  write 0, write b(i)
write 1
```

Decoding:

```
v = 1
while read_bit() == 0:
    v = 2*v + read_bit()
```

Examples: 1 = `1`; 2 = `0 0 1`; 3 = `0 1 1`; 4 = `0 0 0 0 1`; 7 = `0 1 0 1 1`.
A value of m+1 significant bits costs 2m+1 bits. The Z80 decoders hold the value in a 16-bit
register, so lengths are limited to 1..65535.

## 3. Grammar (state machine)

The decoder state is the output position, the **last offset** (initially 1) and one of three
states. The stream always starts in state LIT.

```
state LIT:
    n = gamma()
    copy n raw bytes from the stream to the output
    if read_bit() == 0: state = REP  else: state = NEW

state REP:
    n = gamma()
    copy n bytes from (output - last_offset)
    if read_bit() == 0: state = LIT  else: state = NEW

state NEW:
    k = read 4 bits (MSB first)
    if k == 15: stop                       (end marker)
    extra = read widths[k] bits (MSB first; none if widths[k] == 0)
    last_offset = base[k] + extra
    n = gamma() + 1                        (n >= 2)
    copy n bytes from (output - last_offset)
    if read_bit() == 0: state = LIT  else: state = NEW
```

Notes:

- There is no flag after a literal run that says "match": a literal run is always followed
  by a match (REP or NEW) or by the end, because two literal runs in a row would be one run.
- REP is only reachable right after a literal run (a REP after a match would just be a longer
  match). After a match, the bit means "literals (0) or new offset (1)".
- Copies are byte by byte, forward, so `n` may exceed the offset (overlapping copy:
  offset 1 repeats the last byte, offset 2 a 2-byte pattern, and so on).
- The end marker is a NEW token with index 15, so it always follows a flag bit 1.
- The format cannot represent an empty file: the first token is a literal run of at least
  1 byte. Encoders must refuse empty input (or the container stores it as "no stream").

## 4. Offset buckets and table format

### 4.1 Buckets

The 15 buckets are contiguous and start at offset 1:

```
base[0]   = 1
base[k+1] = base[k] + 2^widths[k]        (k = 0..13)
bucket k covers offsets base[k] .. base[k] + 2^widths[k] - 1
```

Each offset has exactly one bucket. Offsets beyond the last bucket cannot be coded (the
encoder never uses them). The reference encoder limits offsets to 16383 (TMS9918A VRAM is
16 KB); the format itself allows any offset that fits in the buckets and below 65536.

`widths[k]` is 0..14. A width of 0 makes a one-offset bucket (no extra bits).

### 4.2 The 15-byte table (`.tabz`)

```
byte k (k = 0..14) = widths[k] + 1          (values 1..15)
```

The stored value is `width + 1` so that the small Z80 decoders can count it down with `djnz`.
The small decoders (`amyz_vram.asm`, `amyz_ram.asm`) read this table at the label
`amyz_widths` and compute `base[k]` every time they decode a new offset.

### 4.3 The expanded 45-byte table (`.tabzx`)

For the fast decoders (`amyzf_vram.asm`, `amyzf_ram.asm`), the build tool expands the 15-byte
table into precomputed bases. It is not part of the stream and not stored in the `.amyz` file:

```
entry k at bytes 3k, 3k+1, 3k+2 (k = 0..14):
    byte 3k     = (1 - base[k]) & 0xFF
    byte 3k + 1 = ((1 - base[k]) >> 8) & 0xFF      (16-bit, modulo 65536)
    byte 3k + 2 = widths[k]                         (not + 1)
```

The base is stored as `1 - base[k]` so that the decoder gets the negated offset with one
addition: it shifts the complemented extra bits into a register preset to `$FFFF` (giving
`~extra = -extra - 1`), then `(1 - base) + (-extra - 1) = -(base + extra) = -offset`.

Expansion code: `expandTable(t15)` in `js/amyz.js`, `expand_table(t15)` in `python/amyz.py`
(`amyz.py x game.tabz game.tabzx`). The two are checked equal on all 15 game tables.

### 4.4 Tiny mode (fixed log2 table)

Tiny mode uses `widths[k] = k` with no stored table. Then `base[k] = 2^k`, and an offset is
coded as its bit length minus 1 (`k`, 4 bits) followed by the `k` bits under its leading 1.
A tiny-mode stream is an ordinary AMYZ stream for the table `01 02 03 ... 0F`.

## 5. Worked example (tiny mode)

Input (11 bytes): `ABC-ABC+ABC`. Compressed with tiny mode, 9 bytes:

```
offset: 0  1  2  3  4  5  6  7  8
byte:   0C 41 42 43 2D 82 9F 2B C0
```

Decoding, bit by bit (state starts at LIT, last offset 1):

| step | stream byte read | bits | meaning |
|---|---|---|---|
| 1 | byte 0 `0C` = `00001100` as bit byte | `0 0, 0 0, 1` | gamma: v=1, (0,0) v=2, (0,0) v=4, stop: **LIT 4** |
| 2 | bytes 1-4 raw | | output `A B C -` |
| 3 | (byte 0, bit 5) | `1` | flag after literals: NEW |
| 4 | (byte 0, bits 6-7) | `0 0` | first two index bits; the bit byte is now empty |
| 5 | byte 5 `82` = `10000010` as bit byte | `1 0` | index = `0010` = 2 |
| 6 | (byte 5) | `0 0` | widths[2] = 2 extra bits: 0; offset = base[2] + 0 = 4 |
| 7 | (byte 5) | `0 0, 1` | gamma = 2, length = 2 + 1 = 3: **NEW offset 4, length 3**, output `A B C` |
| 8 | (byte 5, bit 7) | `0` | flag after match: LIT |
| 9 | byte 6 `9F` = `10011111` as bit byte | `1` | gamma = 1: **LIT 1** |
| 10 | byte 7 raw | | output `+` |
| 11 | (byte 6) | `0` | flag after literals: REP |
| 12 | (byte 6) | `0 1, 1` | gamma = 3: **REP length 3** with offset 4, output `A B C` |
| 13 | (byte 6) | `1` | flag after match: NEW |
| 14 | (byte 6, bits 6-7) | `1 1` | first two index bits; the bit byte is now empty |
| 15 | byte 8 `C0` = `11000000` as bit byte | `1 1` | index = `1111` = 15: **end** (the last 6 bits are padding) |

Output: `ABC-ABC+ABC`. Note in step 5 how the second bit byte comes after the four raw bytes,
because it was first needed after them. This trace is produced by a reference decoder; the
stream was made by `compressTiny` in `js/amyz.js`.

With a table instead of tiny mode, only steps 5-6 change: the index selects `widths[k]` and
`base[k]` from the table.

## 6. Encoder notes

The reference encoder (`js/amyz.js` and `js/amyz_core.js`; `python/amyz.py` produces the same
bytes) has two layers.

**Optimal parser.** For a given table, the parse is ZX0's optimal parser (Einar Saukas's
`optimize.c` dynamic programming) with an offset-dependent cost for NEW tokens:
`1 + 4 + widths[k]` bits for the flag, index and extra bits, plus the gamma length cost. It keeps,
for every output position and every offset, the cheapest chain ending in a literal run or in a
match with that offset, so the choice between REP (cheap, needs a preceding literal run) and NEW
is exact. The result is the smallest stream for that table under this grammar.

**Fit / parse iterations (Exomizer-style).** The table and the parse depend on each other. The
encoder alternates:

1. parse every file of the group with the current table;
2. histogram the offsets of all NEW tokens;
3. compute the optimal 15-bucket table for that histogram (dynamic programming over (bucket,
   base), minimising the sum of count x (4 + width) bits);
4. repeat while the total size of the group shrinks (at most 8 passes), keep the best.

This is a local search, so the start table matters. The fit runs from three start tables
(the classic default `0,1,1,2,3,...,13`, the log2 table, and a table trained on a corpus) and
keeps the smallest result. That gains 0-5 B per game over a single start. One table is fitted
per group (a game), since sharing it costs 15 B once instead of once per file.

**Why greedy is worse.** A greedy (longest match first) parser does not see the cost of the
offset bits, so it takes long far matches whose 4 + width bits plus gamma cost more than a
shorter near match followed by a REP; it also cannot plan a literal run so that the next match
becomes a cheap REP, which is the point of ZX0's grammar. Measured on the 200-file test corpus
(`tests/greedy_vs_optimal.mjs`, `tests/logs/greedy_vs_optimal.txt`): a greedy parser (longest
match, up to 256 hash-chain candidates, last offset preferred on ties), encoded with the same
per-game tables, gives 90121 B against 77461 B for the optimal parser: +16.3% in all
(+10.7% to +18.3% per game). The greedy run reuses the tables fitted for the optimal parse
(no refit for greedy). Not measured, but expected: greedy also weakens the fit loop, because
its offset histogram reflects match length only, not the cost of each offset, so a refit has
less to gain.

Encoding speed: the JS encoder fits all 15 games of the test corpus (339 KB) in about 4 minutes
(measured, one core). The Python port is much slower (about 1 minute per pass on a 6 KB file) and is
meant as a readable reference.

## 7. Decoders

All decoders read the same streams. Entry: HL = stream, DE = destination.

| option | file | target | table | code | notes |
|---|---|---|---|---|---|
| `amyz` | `z80/amyz_vram.asm` | VRAM | 15 B at `amyz_widths` | 182 B | computes bases |
| `amyz fast` | `z80/amyzf_vram.asm` | VRAM | 45 B at `amyzf_bases` | 203 B | inlined bits, djnz burst copy |
| `amyz tiny` | `z80/amyzt_vram.asm` | VRAM | none (log2) | 152 B | tiny-mode streams only |
| `amyz ram` | `z80/amyz_ram.asm` | RAM | 15 B at `amyz_widths` | 124 B | LDIR copies |
| `amyz ram fast` | `z80/amyzf_ram.asm` | RAM | 45 B at `amyzf_bases` | 134 B | LDIR, inlined bits |

The VRAM decoders keep at least 29 T-states between VRAM data accesses (TMS9918A rule in
the worst case of graphics modes). The RAM decoders are not for in-place decoding.

## 8. Results

All figures MEASURED unless marked. Corpus: 200 files of 15 games (ColecoVision patterns,
colours, name tables and comic screens, plus the Warrior bitmap), 339,608 + 12,288 bytes.
zx0 and Exomizer 2 data sizes and their T/byte are from the earlier study on the same corpus
and harness; the Warrior Exomizer 2 size (2820 B) was measured as a size only, without a round
trip.

### 8.1 Net cost per game (bytes: data + decoder + table)

Each column ships one decoder and one table per game: zx0 135 B; Exomizer 2 226 B (+166 B of
RAM for its table); AMYZ 182 B + 15 B table; AMYZ fast 203 B + 45 B expanded table; AMYZ tiny
152 B, no table. Data sizes are identical for AMYZ and AMYZ fast (same streams).

| game | raw | zx0 | exo2 | AMYZ (182+15) | AMYZ fast (203+45) | AMYZ tiny (152) | AMYZ vs zx0 | AMYZ vs exo2 |
|---|---|---|---|---|---|---|---|---|
| SUBLEVEL 9 | 244208 | 46598 | 45248 | 44495 | 44546 | 46773 | -4.5% | -1.7% |
| sehan-awari | 22904 | 7883 | 8346 | 7567 | 7618 | 7748 | -4.0% | -9.3% |
| sehan-grenouille | 13600 | 4778 | 4806 | 4597 | 4648 | 4732 | -3.8% | -4.3% |
| Warrior | 12288 | 2984 | 3046 | 2968 | 3019 | 2998 | -0.5% | -2.6% |
| 24-bounce-away | 8536 | 2829 | 2965 | 2752 | 2803 | 2822 | -2.7% | -7.2% |
| sehan-incendie | 6480 | 2704 | 2788 | 2622 | 2673 | 2671 | -3.0% | -6.0% |
| 02-dont-cry-wolf | 6336 | 2285 | 2412 | 2251 | 2302 | 2287 | -1.5% | -6.7% |
| 21-space-chase | 6000 | 2034 | 2148 | 2002 | 2053 | 2015 | -1.6% | -6.8% |
| 03-moon-landing | 5600 | 2040 | 2130 | 1985 | 2036 | 2013 | -2.7% | -6.8% |
| sehan-champ-de-carottes | 5112 | 1707 | 1839 | 1674 | 1725 | 1698 | -1.9% | -9.0% |
| sehan-piranhas | 4640 | 1729 | 1844 | 1721 | 1772 | 1733 | -0.5% | -6.7% |
| sehan-canadair | 4352 | 1545 | 1673 | 1533 | 1584 | 1534 | -0.8% | -8.4% |
| sehan-chien-de-berger | 4144 | 1500 | 1622 | 1503 | 1554 | 1502 | +0.2% | -7.3% |
| 04-bird-and-caterpillar | 4048 | 1448 | 1566 | 1441 | 1492 | 1446 | -0.5% | -8.0% |
| merge-chenille-snake | 3648 | 1312 | 1442 | 1305 | 1356 | 1306 | -0.5% | -9.5% |

Sum of the 15 games (each with its own decoder): zx0 83376, Exomizer 2 83875, **AMYZ 80416
(-3.6% vs zx0)**, AMYZ fast 81181 (-2.6%), AMYZ tiny 83278 (-0.1%).

AMYZ beats zx0 net in 14 of 15 games, Warrior included (2968 against 2984). The exception is
sehan-chien-de-berger (+3 B; tiny mode is +2 B). It beats Exomizer 2 in all 15.

### 8.2 Decoders (Z80 harness, 200 files incl. Warrior)

Every run: 200/200 files decoded and compared byte for byte, 0 failures. VRAM runs: 0 data
accesses closer than 29 T (smallest gap 34 T). RAM runs: output compared in RAM, no VDP port
access. "Stack" is the deepest stack use including the caller's 2-byte return address;
"RAM written" counts the bytes the decoder itself writes (stack only, no other RAM).

| decoder | code | table | T/byte all | patterns | colours | names | comic | Warrior | stack | RAM written |
|---|---|---|---|---|---|---|---|---|---|---|
| `amyz` (VRAM) | 182 B | 15 B | 345.2 | 443.9 | 377.2 | 281.2 | 344.7 | 270.4 | 8 | 6 |
| `amyz fast` (VRAM) | 203 B | 45 B | **206.3** | 259.1 | 241.8 | 178.6 | 202.9 | 165.5 | 8 | 6 |
| `amyz tiny` (VRAM) | 152 B | 0 | 246.3 | 292.9 | 280.9 | 216.7 | 244.2 | 205.6 | 8 | 6 |
| `amyz ram` | 124 B | 15 B | 212.3 | 319.8 | 244.1 | 156.4 | 208.5 | 145.3 | 8 | 6 |
| `amyz ram fast` | 134 B | 45 B | **103.8** | 154.0 | 133.5 | 82.8 | 99.4 | 71.6 | 6 | 4 |
| zx0 VRAM (earlier study, 198 files, no Warrior) | 135 B | 0 | 206 | 219 | 221 | 191 | 206 | not run | | |
| Exomizer 2 VRAM (earlier study, 198 files) | 226 B | +166 B RAM | 354 | 489 | 451 | 334 | 328 | not run | | |

The tiny-mode streams are 3.6% larger than table-mode streams (80998 against 77461 B), which
is why its T/byte is not directly comparable. `amyz fast` decodes at zx0's speed with 3.6% less
data; `amyz` trades speed for size (its base walk runs at every new offset).
