#!/usr/bin/env python3
"""AMYZ core (pure Python port of js/amyz_core.js, same output): optimal parser, bucket fit,
bit-stream writer and reader. Internal module used by amyz.py; see SPEC.md.
Format by Amy (AmyPurple), with Claude. Slow on 6 KB files (about 1 min per pass)."""
import os, sys

NBUCKETS = 15
END_INDEX = 15
MAX_OFFSET = 16383
INF = 0x3fffffff
DEFAULT_WIDTHS = [0, 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13]
MAX_PASSES = 8
INDEX_GAMMA = False  # research knob (gamma-coded index), off in the AMYZ format


def gamma_bits(v):
    b = 1
    while v > 1:
        b += 2; v >>= 1
    return b


def idx_bits(k):
    return gamma_bits(k + 1) if INDEX_GAMMA else 4


def widths_to_bases(widths):
    out, b = [], 1
    for w in widths:
        out.append(b); b += 1 << w
    return out


class Table:
    def __init__(self, widths, bases=None):
        self.widths = list(widths); self.bases = list(bases) if bases else widths_to_bases(widths)

    @staticmethod
    def from_bytes(t):
        return Table([t[3 * k + 2] for k in range(NBUCKETS)], [t[3 * k] | t[3 * k + 1] << 8 for k in range(NBUCKETS)])

    def to_bytes(self):
        out = bytearray()
        for b, w in zip(self.bases, self.widths):
            out += bytes([b & 255, (b >> 8) & 255, w])
        return bytes(out)

    def bucket_of(self, o):
        for k in range(NBUCKETS):
            if o >= self.bases[k] and o - self.bases[k] < (1 << self.widths[k]):
                return k
        raise ValueError("offset not coded %d" % o)


def offset_costs(t):
    cost = [INF] * (MAX_OFFSET + 1)
    for k in range(NBUCKETS):
        end = min(t.bases[k] + (1 << t.widths[k]), MAX_OFFSET + 1)
        c = 1 + idx_bits(k) + t.widths[k]
        for o in range(t.bases[k], end):
            if cost[o] == INF:
                cost[o] = c
    return cost


class Block:
    __slots__ = ("bits", "index", "offset", "chain")

    def __init__(self, bits, index, offset, chain):
        self.bits = bits; self.index = index; self.offset = offset; self.chain = chain


def optimize(data, t):
    """ZX0 optimize.c with an offset-dependent new-offset cost; returns the last block"""
    n = len(data); ocost = offset_costs(t)
    cover = 0
    while cover + 1 <= MAX_OFFSET and ocost[cover + 1] < INF:
        cover += 1
    size = min(max(n - 1, 1), cover) + 1
    last_literal = [None] * size; last_match = [None] * size; match_length = [0] * size
    optimal = [None] * n; best_length = [0] * (n + 2)
    if n > 2:
        best_length[2] = 2
    last_match[1] = Block(-1, -1, 1, None)
    gb = [gamma_bits(v) if v else 0 for v in range(n + 2)]
    for index in range(n):
        max_offset = min(max(index, 1), cover)
        best = None; bls = 2; cur = data[index]
        for offset in range(1, max_offset + 1):
            if index and index >= offset and cur == data[index - offset]:
                lit = last_literal[offset]
                if lit is not None:
                    bits = lit.bits + 1 + gb[index - lit.index]
                    blk = Block(bits, index, offset, lit); last_match[offset] = blk
                    if best is None or best.bits > bits:
                        best = blk
                match_length[offset] += 1; ml = match_length[offset]
                if ml > 1:
                    if bls < ml:
                        bits = optimal[index - best_length[bls]].bits + gb[best_length[bls] - 1]
                        while True:
                            bls += 1
                            bits2 = optimal[index - bls].bits + gb[bls - 1]
                            if bits2 <= bits:
                                best_length[bls] = bls; bits = bits2
                            else:
                                best_length[bls] = best_length[bls - 1]
                            if bls >= ml:
                                break
                    length = best_length[ml]
                    bits = optimal[index - length].bits + ocost[offset] + gb[length - 1]
                    lm = last_match[offset]
                    if lm is None or lm.index != index or lm.bits > bits:
                        blk = Block(bits, index, offset, optimal[index - length]); last_match[offset] = blk
                        if best is None or best.bits > bits:
                            best = blk
            else:
                match_length[offset] = 0
                lm = last_match[offset]
                if lm is not None:
                    length = index - lm.index
                    bits = lm.bits + 1 + gb[length] + 8 * length
                    blk = Block(bits, index, 0, lm); last_literal[offset] = blk
                    if best is None or best.bits > bits:
                        best = blk
        optimal[index] = best
    return optimal[n - 1]


def chain_to_tokens(last):
    blocks = []
    b = last
    while b is not None:
        blocks.append(b); b = b.chain
    blocks.reverse()
    toks = []; last_offset = 1
    for i in range(1, len(blocks)):
        prev, cur = blocks[i - 1], blocks[i]; ln = cur.index - prev.index
        if cur.offset == 0:
            toks.append(("lit", prev.index + 1, ln))
        elif cur.offset == last_offset and toks and toks[-1][0] == "lit":
            toks.append(("rep", ln))
        else:
            toks.append(("new", cur.offset, ln)); last_offset = cur.offset
    return toks


class BitWriter:
    def __init__(self):
        self.out = bytearray(); self.pos = -1; self.mask = 0

    def bit(self, b):
        if self.mask == 0:
            self.out.append(0); self.pos = len(self.out) - 1; self.mask = 0x80
        if b:
            self.out[self.pos] |= self.mask
        self.mask >>= 1

    def bits(self, v, n):
        for k in range(n - 1, -1, -1):
            self.bit(v >> k & 1)

    def gamma(self, v):
        for k in range(v.bit_length() - 2, -1, -1):
            self.bit(0); self.bit(v >> k & 1)
        self.bit(1)

    def idx(self, k):
        self.gamma(k + 1) if INDEX_GAMMA else self.bits(k, 4)


def encode_tokens(data, toks, t):
    w = BitWriter()
    for i, tok in enumerate(toks):
        nxt = toks[i + 1][0] if i + 1 < len(toks) else "end"
        if tok[0] == "lit":
            w.gamma(tok[2]); w.out += data[tok[1]:tok[1] + tok[2]]
            w.bit(0 if nxt == "rep" else 1)
        else:
            if tok[0] == "rep":
                w.gamma(tok[1])
            else:
                o = tok[1]; k = t.bucket_of(o)
                w.idx(k); w.bits(o - t.bases[k], t.widths[k]); w.gamma(tok[2] - 1)
            w.bit(0 if nxt == "lit" else 1)
    w.idx(END_INDEX)
    return bytes(w.out)


def fit_table(hist):
    """optimal bucket fit of {offset: count}, see fitTable in amyz_core.js"""
    top = max(hist) if hist else 0
    if top == 0:
        return Table(DEFAULT_WIDTHS)
    pre = [0] * (top + 2)
    for o in range(1, top + 1):
        pre[o + 1] = pre[o] + hist.get(o, 0)
    memo = {}

    def go(k, base):
        if base > top:
            return (0, ())
        if k == NBUCKETS:
            return (INF, ())
        key = (k, base)
        if key in memo:
            return memo[key]
        r = (INF, ())
        for b in range(15):
            cnt = pre[min(base + (1 << b), top + 1)] - pre[base]
            s = go(k + 1, base + (1 << b))
            c = s[0] + cnt * (b + idx_bits(k))
            if c < r[0]:
                r = (c, (b,) + s[1])
            if base + (1 << b) > top:
                break
        memo[key] = r
        return r
    widths = list(go(0, 1)[1])
    widths += [14] * (NBUCKETS - len(widths))
    bases = widths_to_bases(widths)
    if not INDEX_GAMMA:
        return Table(widths, bases)
    ent = [(-(pre[min(bases[k] + (1 << w), top + 1)] - pre[min(bases[k], top + 1)]), k, w, bases[k]) for k, w in enumerate(widths)]
    ent.sort()
    return Table([e[2] for e in ent], [e[3] for e in ent])


def compress_with(data, t):
    toks = chain_to_tokens(optimize(data, t)) if data else []
    return toks, encode_tokens(data, toks, t)


def fit_group(datas, passes=MAX_PASSES, start=None, log=None):
    t = start or Table(DEFAULT_WIDTHS); best = None; seen = set()
    for p in range(passes):
        key = (tuple(t.widths), tuple(t.bases))
        if key in seen:
            break
        seen.add(key); hist = {}; streams = []
        for d in datas:
            toks, z = compress_with(d, t); streams.append(z)
            for x in toks:
                if x[0] == "new":
                    hist[x[1]] = hist.get(x[1], 0) + 1
        size = sum(map(len, streams))
        if log:
            log(f"pass {p + 1}: {size} bytes")
        if best is None or size < best[0]:
            best = (size, t, streams, p + 1)
        else:
            break
        t = fit_table(hist)
    return best[1], best[2]


def decode(z, t, start=0):
    st = [start, 0, 0]  # index, mask, byte
    out = bytearray()

    def bit():
        if st[1] == 0:
            st[2] = z[st[0]]; st[0] += 1; st[1] = 0x80
        r = 1 if st[2] & st[1] else 0
        st[1] >>= 1
        return r

    def bits(n):
        v = 0
        for _ in range(n):
            v = v << 1 | bit()
        return v

    def gamma():
        v = 1
        while not bit():
            v = v << 1 | bit()
        return v
    last = 1; state = 0
    while True:
        if state == 0:
            n = gamma(); out += z[st[0]:st[0] + n]; st[0] += n
            state = 2 if bit() else 1
        else:
            if state == 1:
                n = gamma()
            else:
                k = gamma() - 1 if INDEX_GAMMA else bits(4)
                if k == END_INDEX:
                    return bytes(out)
                last = t.bases[k] + bits(t.widths[k]); n = gamma() + 1
            for _ in range(n):
                out.append(out[-last])
            state = 2 if bit() else 0
