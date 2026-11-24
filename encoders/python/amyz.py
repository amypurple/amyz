#!/usr/bin/env python3
"""AMYZ reference encoder/decoder (pure Python, same output as js/amyz.js).

Table mode: 15-byte table, byte k = widths[k] + 1, and the
fit tries three start tables. Tiny mode: no table, fixed log2 table widths[k] = k.
Needs amyz_core.py next to this file (a copy of python/amyz_core.py).

usage:
  amyz.py c IN OUT TABLE15     compress with a table
  amyz.py d IN OUT TABLE15     decompress with a table
  amyz.py ct IN OUT            compress, tiny mode
  amyz.py dt IN OUT            decompress, tiny mode
  amyz.py g OUTDIR FILE...     fit one table for a group: OUTDIR/group.tabz and FILE.amyz
  amyz.py x TABLE15 OUT45      expand a 15-byte table for the fast decoders (amyzf_*.asm)

Expanded table (45 bytes), entry k = 0..14 at offset 3k: (1 - base[k]) mod 65536 little endian,
then widths[k]; base[0] = 1, base[k+1] = base[k] + 2^widths[k].
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import amyz_core as core

LOG2_WIDTHS = list(range(15))
STARTS = [[0, 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13], LOG2_WIDTHS,
          [0, 2, 1, 0, 3, 4, 4, 5, 5, 7, 3, 3, 8, 10, 13]]


def table15(widths):
    return bytes(w + 1 for w in widths)


def widths_of(t15):
    return [b - 1 for b in t15[:15]]


def expand_table(t15):
    out = bytearray(); base = 1
    for w in widths_of(t15):
        v = (1 - base) & 0xFFFF
        out += bytes((v & 0xFF, v >> 8, w)); base += 1 << w
    return bytes(out)


def fit_group(datas):
    best = None
    for s in STARTS:
        t, streams = core.fit_group(datas, start=core.Table(s))
        size = sum(map(len, streams))
        if best is None or size < best[0]:
            best = (size, t, streams)
    return table15(best[1].widths), best[2]


def compress_table(data, t15):
    return core.compress_with(data, core.Table(widths_of(t15)))[1]


def decompress_table(z, t15):
    return core.decode(z, core.Table(widths_of(t15)))


def compress_tiny(data):
    return core.compress_with(data, core.Table(LOG2_WIDTHS))[1]


def decompress_tiny(z):
    return core.decode(z, core.Table(LOG2_WIDTHS))


def main(argv):
    if len(argv) < 3:
        print(__doc__); return 1
    cmd = argv[0]
    if cmd == "g":
        files = argv[2:]; t15, streams = fit_group([open(f, "rb").read() for f in files])
        os.makedirs(argv[1], exist_ok=True)
        open(os.path.join(argv[1], "group.tabz"), "wb").write(t15)
        open(os.path.join(argv[1], "group.tabzx"), "wb").write(expand_table(t15))
        for f, z in zip(files, streams):
            open(os.path.join(argv[1], os.path.basename(f) + ".amyz"), "wb").write(z)
        return 0
    d = open(argv[1], "rb").read()
    if cmd in ("c", "d"):
        t15 = open(argv[3], "rb").read()
        out = compress_table(d, t15) if cmd == "c" else decompress_table(d, t15)
    elif cmd == "x":
        out = expand_table(d)
    elif cmd in ("ct", "dt"):
        out = compress_tiny(d) if cmd == "ct" else decompress_tiny(d)
    else:
        print(__doc__); return 1
    open(argv[2], "wb").write(out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
