/**
 * AMYZ core: optimal parser, bucket fit, bit-stream writer and reader (2026).
 * Format by Amy (AmyPurple), continuing her DAN1-DAN3 line, with Claude.
 * Ideas from ZX0 (Einar Saukas: optimal parser, implicit-state grammar, repeat offset)
 * and Exomizer (Magnus Lind: adaptive offset buckets).
 * Internal module used by amyz.js; see SPEC.md for the format.
 */

const NBUCKETS = 15;
const END_INDEX = 15;
let MAX_OFFSET = 16383;             // TMS9918A VRAM is 16 KB (amyzpack2: setMaxOffset)
export function setMaxOffset(v) { MAX_OFFSET = v; }
const INF = 0x3fffffff;
/* Speed-aware cost model (2026-11, amyz -speed). Every cost is in model units:
 * bit = cost of one stream bit, litByte = cost of one raw literal byte, offBit = cost
 * of one index/extra bit, plus per-token and per-copied-byte decoder costs.
 * The default (bit 1, litByte 8, offBit 1, others 0) is the plain bit count, which
 * reproduces the size-optimal parser exactly. */
const DEFAULT_MODEL = { bit: 1, litByte: 8, offBit: 1, litTok: 0, repTok: 0, newTok: 0, matchByte: 0 };
let M = DEFAULT_MODEL;
export function setCostModel(m) { M = m ? { ...DEFAULT_MODEL, ...m } : DEFAULT_MODEL; }
const gammaC = (v) => gammaBits(v) * M.bit;
const DEFAULT_WIDTHS = [0, 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13];
const MAX_PASSES = 8;

const INDEX_GAMMA = false; // research knob (gamma-coded index), off in the AMYZ format;
const idxBits = (k) => (INDEX_GAMMA ? gammaBits(k + 1) : 4);
const writeIdx = (w, k) => (INDEX_GAMMA ? w.gamma(k + 1) : w.bits(k, 4));
const bucketOf = (t, o) => { for (let k = 0; k < NBUCKETS; k++) if (o >= t.bases[k] && o - t.bases[k] < (1 << t.widths[k])) return k; throw new Error('offset not coded ' + o); };
const gammaBits = (v) => { let b = 1; while (v > 1) { b += 2; v >>= 1; } return b; };

export function widthsToBases(widths) {
    const bases = []; let b = 1;
    for (const w of widths) { bases.push(b); b += 1 << w; }
    return bases;
}

export function tableBytes(t) {
    const out = new Uint8Array(NBUCKETS * 3); const bases = t.bases, widths = t.widths;
    for (let k = 0; k < NBUCKETS; k++) {
        out[3 * k] = bases[k] & 255; out[3 * k + 1] = (bases[k] >> 8) & 255; out[3 * k + 2] = widths[k];
    }
    return out;
}

function tableFromOption(t) {
    if (!t) return null;
    if (t.length === NBUCKETS) return { widths: Array.from(t), bases: widthsToBases(Array.from(t)) };
    if (t.length < NBUCKETS * 3) throw new Error('table must be 45 bytes or 15 widths');
    const widths = [], bases = [];
    for (let k = 0; k < NBUCKETS; k++) { bases.push(t[3 * k] | t[3 * k + 1] << 8); widths.push(t[3 * k + 2]); }
    return { widths, bases };
}

/** cost in bits of a new-offset token's flag + index + extra bits, per offset (INF = not coded) */
function offsetCosts(t) {
    const cost = new Int32Array(MAX_OFFSET + 1).fill(INF);
    for (let k = 0; k < NBUCKETS; k++) {
        const end = Math.min(t.bases[k] + (1 << t.widths[k]), MAX_OFFSET + 1);
        for (let o = t.bases[k]; o < end; o++) if (cost[o] === INF) cost[o] = M.bit + M.offBit * (idxBits(k) + t.widths[k]) + M.newTok;
    }
    return cost;
}

/** ZX0's optimizer (optimize.c) with an offset-dependent new-offset cost. Returns the last block. */
export function optimize(input, t) {
    const n = input.length; const ocost = offsetCosts(t);
    let cover = 0; while (cover + 1 <= MAX_OFFSET && ocost[cover + 1] < INF) cover++;
    const size = Math.min(Math.max(n - 1, 1), cover) + 1;
    const lastLiteral = new Array(size).fill(null);
    const lastMatch = new Array(size).fill(null);
    const matchLength = new Int32Array(size);
    const optimal = new Array(n).fill(null);
    const bestLength = new Int32Array(n + 2);
    if (n > 2) bestLength[2] = 2;
    lastMatch[1] = { bits: -M.bit, index: -1, offset: 1, chain: null };
    for (let index = 0; index < n; index++) {
        const maxOffset = Math.min(Math.max(index, 1), cover);
        let best = null; let bestLengthSize = 2;
        for (let offset = 1; offset <= maxOffset; offset++) {
            if (index !== 0 && index >= offset && input[index] === input[index - offset]) {
                const lit = lastLiteral[offset];
                if (lit !== null) {
                    const bits = lit.bits + M.bit + gammaC(index - lit.index) + M.repTok + M.matchByte * (index - lit.index);
                    const blk = { bits, index, offset, chain: lit };
                    lastMatch[offset] = blk;
                    if (best === null || best.bits > bits) best = blk;
                }
                if (++matchLength[offset] > 1) {
                    if (bestLengthSize < matchLength[offset]) {
                        let bits = optimal[index - bestLength[bestLengthSize]].bits + gammaC(bestLength[bestLengthSize] - 1) + M.matchByte * bestLength[bestLengthSize];
                        do {
                            bestLengthSize++;
                            const bits2 = optimal[index - bestLengthSize].bits + gammaC(bestLengthSize - 1) + M.matchByte * bestLengthSize;
                            if (bits2 <= bits) { bestLength[bestLengthSize] = bestLengthSize; bits = bits2; }
                            else bestLength[bestLengthSize] = bestLength[bestLengthSize - 1];
                        } while (bestLengthSize < matchLength[offset]);
                    }
                    const length = bestLength[matchLength[offset]];
                    const bits = optimal[index - length].bits + ocost[offset] + gammaC(length - 1) + M.matchByte * length;
                    const lm = lastMatch[offset];
                    if (lm === null || lm.index !== index || lm.bits > bits) {
                        const blk = { bits, index, offset, chain: optimal[index - length] };
                        lastMatch[offset] = blk;
                        if (best === null || best.bits > bits) best = blk;
                    }
                }
            } else {
                matchLength[offset] = 0;
                const lm = lastMatch[offset];
                if (lm !== null) {
                    const length = index - lm.index;
                    const bits = lm.bits + M.bit + gammaC(length) + M.litByte * length + M.litTok;
                    const blk = { bits, index, offset: 0, chain: lm };
                    lastLiteral[offset] = blk;
                    if (best === null || best.bits > bits) best = blk;
                }
            }
        }
        optimal[index] = best;
    }
    return optimal[n - 1];
}

/** block chain -> tokens [kind, a, b]: ['lit', start, len] | ['rep', len] | ['new', offset, len] */
export function chainToTokens(last) {
    const blocks = []; for (let b = last; b !== null; b = b.chain) blocks.push(b);
    blocks.reverse();
    const toks = []; let lastOffset = 1;
    for (let i = 1; i < blocks.length; i++) {
        const prev = blocks[i - 1], cur = blocks[i]; const len = cur.index - prev.index;
        if (cur.offset === 0) toks.push(['lit', prev.index + 1, len]);
        else if (cur.offset === lastOffset && toks.length && toks[toks.length - 1][0] === 'lit') toks.push(['rep', len]);
        else { toks.push(['new', cur.offset, len]); lastOffset = cur.offset; }
    }
    return toks;
}

class BitWriter {
    constructor() { this.out = []; this.pos = -1; this.mask = 0; }
    bit(b) {
        if (this.mask === 0) { this.out.push(0); this.pos = this.out.length - 1; this.mask = 0x80; }
        if (b) this.out[this.pos] |= this.mask;
        this.mask >>= 1;
    }
    bits(v, n) { for (let k = n - 1; k >= 0; k--) this.bit((v >> k) & 1); }
    gamma(v) {
        const nb = 32 - Math.clz32(v);
        for (let k = nb - 2; k >= 0; k--) { this.bit(0); this.bit((v >> k) & 1); }
        this.bit(1);
    }
    byte(v) { this.out.push(v & 255); }
}

export function encodeTokens(input, toks, tab) {
    const w = new BitWriter();
    for (let t = 0; t < toks.length; t++) {
        const tok = toks[t]; const nxt = t + 1 < toks.length ? toks[t + 1][0] : 'end';
        if (tok[0] === 'lit') {
            w.gamma(tok[2]); for (let i = 0; i < tok[2]; i++) w.byte(input[tok[1] + i]);
            w.bit(nxt === 'rep' ? 0 : 1);
        } else {
            if (tok[0] === 'rep') w.gamma(tok[1]);
            else {
                const o = tok[1]; const k = bucketOf(tab, o);
                writeIdx(w, k); w.bits(o - tab.bases[k], tab.widths[k]); w.gamma(tok[2] - 1);
            }
            w.bit(nxt === 'lit' ? 0 : 1);
        }
    }
    writeIdx(w, END_INDEX);
    return new Uint8Array(w.out);
}

/** optimal 15-bucket fit of an offset histogram (Map offset -> count): DP over (bucket, base)
 *  minimising sum(count * (index bits + extra bits)); with gamma-coded indexes the used buckets are
 *  then sorted by use so that the most used bucket gets the 1-bit index. Returns {widths, bases}. */
export function fitTable(hist) {
    let top = 0; for (const o of hist.keys()) if (o > top) top = o;
    if (top === 0) return { widths: DEFAULT_WIDTHS.slice(), bases: widthsToBases(DEFAULT_WIDTHS) };
    const pre = new Float64Array(top + 2);
    for (let o = 1; o <= top; o++) pre[o + 1] = pre[o] + (hist.get(o) || 0);
    const memo = new Map();
    const go = (k, base) => {
        if (base > top) return [0, []];
        if (k === NBUCKETS) return [INF, []];
        const key = k * 65536 + base; const m = memo.get(key); if (m) return m;
        let r = [INF, []];
        for (let b = 0; b < 15; b++) {
            const cnt = pre[Math.min(base + (1 << b), top + 1)] - pre[base];
            const s = go(k + 1, base + (1 << b));
            const c = s[0] + cnt * (b + idxBits(k));
            if (c < r[0]) r = [c, [b].concat(s[1])];
            if (base + (1 << b) > top) break;
        }
        memo.set(key, r); return r;
    };
    const widths = go(0, 1)[1];
    while (widths.length < NBUCKETS) widths.push(14);
    const bases = widthsToBases(widths);
    if (!INDEX_GAMMA) return { widths, bases };
    const ent = widths.map((w, k) => ({ w, b: bases[k], c: pre[Math.min(bases[k] + (1 << w), top + 1)] - pre[Math.min(bases[k], top + 1)], k }));
    ent.sort((x, y) => (y.c - x.c) || (x.k - y.k));
    return { widths: ent.map((e) => e.w), bases: ent.map((e) => e.b) };
}

/** Exomizer-style passes: parse all files with a table, refit the table, repeat; keep the best */
export function fitGroup(datas, passes = MAX_PASSES, start = null) {
    let t = start || { widths: DEFAULT_WIDTHS.slice(), bases: widthsToBases(DEFAULT_WIDTHS) };
    let best = null; const seen = new Set();
    for (let p = 0; p < passes; p++) {
        const key = t.widths.join(',') + '/' + t.bases.join(','); if (seen.has(key)) break; seen.add(key);
        const hist = new Map(); const streams = [];
        for (const d of datas) {
            const toks = d.length ? chainToTokens(optimize(d, t)) : [];
            for (const x of toks) if (x[0] === 'new') hist.set(x[1], (hist.get(x[1]) || 0) + 1);
            streams.push(encodeTokens(d, toks, t));
        }
        const size = streams.reduce((a, s) => a + s.length, 0);
        if (best === null || size < best.size) best = { size, t, streams, passes: p + 1 };
        else break;
        t = fitTable(hist);
    }
    return { widths: best.t.widths, bases: best.t.bases, table: tableBytes(best.t), streams: best.streams, size: best.size, passes: best.passes };
}

export function decodeStream(z, widths, bases, start = 0) {
    let i = start, mask = 0, cur = 0; const out = [];
    const bit = () => { if (mask === 0) { cur = z[i++]; mask = 0x80; } const r = cur & mask ? 1 : 0; mask >>= 1; return r; };
    const bits = (n) => { let v = 0; for (let k = 0; k < n; k++) v = v << 1 | bit(); return v; };
    const gamma = () => { let v = 1; while (!bit()) v = v << 1 | bit(); return v; };
    let last = 1; let state = 0; // 0 literals, 1 repeat, 2 new
    for (;;) {
        if (state === 0) {
            const n = gamma(); for (let k = 0; k < n; k++) out.push(z[i++]);
            state = bit() ? 2 : 1;
        } else {
            let n;
            if (state === 1) n = gamma();
            else {
                const k = INDEX_GAMMA ? gamma() - 1 : bits(4); if (k === END_INDEX) return new Uint8Array(out);
                last = bases[k] + bits(widths[k]); n = gamma() + 1;
            }
            for (let k = 0; k < n; k++) out.push(out[out.length - last]);
            state = bit() ? 2 : 0;
        }
    }
}

/* ---- encoder-only search (2026 study): same format, same decoders ---- */

const keyOf = (t) => t.widths.join(',');
const tabOf = (w) => ({ widths: Array.from(w), bases: widthsToBases(Array.from(w)) });

/** width cost (index + extra bits) per offset, INF = not coded */
function widthCosts(t) {
    const c = new Int32Array(MAX_OFFSET + 1).fill(INF);
    for (let k = 0; k < NBUCKETS; k++) {
        const e = Math.min(t.bases[k] + (1 << t.widths[k]), MAX_OFFSET + 1);
        for (let o = t.bases[k]; o < e; o++) if (c[o] === INF) c[o] = idxBits(k) + t.widths[k];
    }
    return c;
}

/** parse every file with table t; result {size, toks, streams, t} */
function parseAll(datas, t) {
    const toks = [], streams = []; let size = 0;
    for (const d of datas) {
        const k = d.length ? chainToTokens(optimize(d, t)) : []; const s = encodeTokens(d, k, t);
        toks.push(k); streams.push(s); size += s.length;
    }
    return { size, toks, streams, t };
}

/** for each new-offset token of a parse: every offset that reproduces it
 *  (and the repeat token that follows it after one literal run, if any) */
export function alternatives(datas, toksAll) {
    const sets = [];
    datas.forEach((d, fi) => {
        const toks = toksAll[fi]; const n = d.length; const idx = new Map(); const pos = []; let p = 0;
        for (const x of toks) { pos.push(p); p += x[0] === 'rep' ? x[1] : x[2]; }
        let next = 0;
        for (let i = 0; i < toks.length; i++) {
            const x = toks[i]; if (x[0] !== 'new') continue;
            const p0 = pos[i], L = x[2];
            while (next < p0) {
                if (next + 1 < n) { const key = d[next] << 8 | d[next + 1]; let a = idx.get(key); if (!a) idx.set(key, a = []); a.push(next); }
                next++;
            }
            let rp = -1, rl = 0;
            if (i + 2 < toks.length && toks[i + 1][0] === 'lit' && toks[i + 2][0] === 'rep') { rp = pos[i + 2]; rl = toks[i + 2][1]; }
            const a = idx.get(d[p0] << 8 | d[p0 + 1]) || []; const set = [];
            for (let j = a.length - 1; j >= 0; j--) {
                const o = p0 - a[j]; if (o > MAX_OFFSET) break;
                let ok = true; for (let m = 2; m < L; m++) if (d[p0 + m] !== d[p0 + m - o]) { ok = false; break; }
                if (ok && rp >= 0) for (let m = 0; m < rl; m++) if (d[rp + m] !== d[rp + m - o]) { ok = false; break; }
                if (ok) set.push(o);
            }
            if (!set.includes(x[1])) set.push(x[1]);
            sets.push({ cur: x[1], set });
        }
    });
    return sets;
}

/** cost-aware refit (hard EM): fit the histogram, move every token to its cheapest
 *  equal-length alternative under the new table, refit, until stable */
export function emFit(sets, iters = 12) {
    const asg = sets.map((a) => a.cur); let t = null, prev = Infinity;
    for (let it = 0; it < iters; it++) {
        const h = new Map(); for (const o of asg) h.set(o, (h.get(o) || 0) + 1);
        const nt = fitTable(h); const c = widthCosts(nt); let changed = 0, obj = 0;
        sets.forEach((a, i) => { let b = asg[i]; for (const o of a.set) if (c[o] < c[b]) b = o; if (b !== asg[i]) { asg[i] = b; changed++; } obj += c[b]; });
        if (obj > prev) break; t = nt; prev = obj; if (!changed) break;
    }
    return t;
}

function surrogate(sets, t) {
    const c = widthCosts(t); let s = 0;
    for (const a of sets) { let m = INF; for (const o of a.set) if (c[o] < m) m = c[o]; s += m; }
    return s;
}

/** fit loop from one start; refit 'hist' (release) or 'em' (cost-aware) */
function fitLoopCached(datas, start, mode, passes, ev) {
    let r = ev(start); let best = r;
    for (let p = 1; p < passes && r; p++) {
        let c;
        if (mode === 'em') c = emFit(alternatives(datas, r.toks));
        else { const h = new Map(); for (const ts of r.toks) for (const x of ts) if (x[0] === 'new') h.set(x[1], (h.get(x[1]) || 0) + 1); c = fitTable(h); }
        if (!c) break; const e = ev(c); if (!e || e.size >= best.size) break; best = r = e;
    }
    return best;
}

/** search: release starts (hist refit), extra starts (em refit), then a local search on
 *  the widths (neighbours ranked by the surrogate, verified by a full parse). Deterministic.
 *  opts.maxEvals bounds the number of full parses spent after the release starts. */
export function fitGroupSearch(datas, starts, extraStarts = [], opts = {}) {
    const cache = new Map(); let budget = Infinity;
    const ev = (t) => { const k = keyOf(t); if (cache.has(k)) return cache.get(k); if (budget <= 0) return null; budget--; const r = parseAll(datas, t); cache.set(k, r); return r; };
    let best = null;
    for (const s of starts) { const r = fitLoopCached(datas, tabOf(s), 'hist', MAX_PASSES, ev); if (!best || r.size < best.size) best = r; }
    const release = best.size; budget = opts.maxEvals ?? 60;
    for (const s of extraStarts) { const r = fitLoopCached(datas, tabOf(s), 'em', MAX_PASSES, ev); if (r && r.size < best.size) best = r; }
    const topk = opts.topk ?? 4, rounds = opts.rounds ?? 6;
    for (let r = 0; r < rounds && budget > 0; r++) {
        const sets = alternatives(datas, best.toks); const w0 = best.t.widths; const cands = new Map();
        const add = (w) => { if (w.some((x) => x < 0 || x > 14)) return; const t = tabOf(w); const k = keyOf(t); if (!cache.has(k) && !cands.has(k)) cands.set(k, t); };
        for (let i = 0; i < NBUCKETS; i++) for (const di of [-1, 1]) {
            const w = w0.slice(); w[i] += di; add(w);
            for (let j = 0; j < NBUCKETS; j++) if (j !== i) { const v = w.slice(); v[j] -= di; add(v); }
        }
        for (let i = 0; i < NBUCKETS; i++) {
            const w = w0.slice(); w.splice(i, 1); w.push(14); add(w);
            for (let b = 0; b <= 14; b++) { const v = w0.slice(); v.splice(i, 0, b); v.pop(); add(v); }
        }
        const sc = [...cands.values()].map((t) => [surrogate(sets, t), keyOf(t), t]).sort((a, b) => a[0] - b[0] || (a[1] < b[1] ? -1 : 1));
        let improved = false;
        for (const [, , t] of sc.slice(0, topk)) { const e = ev(t); if (e && e.size < best.size) { best = e; improved = true; } }
        if (!improved) break;
        const f = fitLoopCached(datas, best.t, 'em', 4, ev); if (f && f.size < best.size) best = f;
    }
    return { widths: best.t.widths, bases: best.t.bases, table: tableBytes(best.t), streams: best.streams, size: best.size, release, evals: cache.size };
}
