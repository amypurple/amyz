/* AMYZ core in C: port of js/amyz_core.js (optimize, chainToTokens, encodeTokens, fitTable,
 * decodeStream). Tie rules are kept identical so that the streams are byte-identical. */
#include "amyzc.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void table_set(Table *t, const int *w) {
    int b = 1;
    for (int k = 0; k < NB; k++) { t->w[k] = w[k]; t->base[k] = b; b += 1 << w[k]; }
}

void table_key(const Table *t, char *buf) {
    char *p = buf;
    for (int k = 0; k < NB; k++) p += sprintf(p, k ? ",%d" : "%d", t->w[k]);
}

static inline int gbits(int v) { return 2 * (31 - __builtin_clz((unsigned)v)) + 1; }
/* speed-aware cost model (amyz -speed): see amyzc.h. Default = plain bit count. */
int CM_bit = 1, CM_lit = 8, CM_off = 1, CM_lt = 0, CM_rt = 0, CM_nt = 0, CM_mb = 0;
#define GC(v) (gbits(v) * CM_bit)

/* ---- block pool with reference counts (ZX0's scheme) ---- */
typedef struct { int bits, index, offset, chain, refs; } Blk;
static Blk *P; static int Pn, Pcap, Pfree = -1;

static inline int blk_new(int bits, int index, int offset, int chain) {
    int b;
    if (Pfree >= 0) { b = Pfree; Pfree = P[b].chain; }
    else {
        if (Pn == Pcap) { Pcap = Pcap ? Pcap * 2 : 1 << 20; P = realloc(P, sizeof(Blk) * (size_t)Pcap); if (!P) { perror("pool"); exit(1); } }
        b = Pn++;
    }
    P[b].bits = bits; P[b].index = index; P[b].offset = offset; P[b].chain = chain; P[b].refs = 0;
    if (chain >= 0) P[chain].refs++;
    return b;
}
static inline void blk_release(int b) {
    while (b >= 0 && --P[b].refs == 0) { int c = P[b].chain; P[b].chain = Pfree; Pfree = b; b = c; }
}
static inline void blk_assign(int *slot, int b) {
    if (b >= 0) P[b].refs++;
    if (*slot >= 0) blk_release(*slot);
    *slot = b;
}

static void toks_push(Toks *tk, int kind, int a, int len) {
    if (tk->n == tk->cap) { tk->cap = tk->cap ? tk->cap * 2 : 256; tk->t = realloc(tk->t, sizeof(Tok) * (size_t)tk->cap); }
    tk->t[tk->n].kind = (uint8_t)kind; tk->t[tk->n].a = a; tk->t[tk->n].len = len; tk->n++;
}

static long chain_to_toks(int last, Toks *out) {
    long bits = P[last].bits;
    int cnt = 0; for (int b = last; b >= 0; b = P[b].chain) cnt++;
    int *bl = malloc(sizeof(int) * (size_t)cnt); { int i = cnt; for (int b = last; b >= 0; b = P[b].chain) bl[--i] = b; }
    out->n = 0; int lastOffset = 1;
    for (int i = 1; i < cnt; i++) {
        Blk *pr = &P[bl[i - 1]], *cu = &P[bl[i]]; int len = cu->index - pr->index;
        if (cu->offset == 0) toks_push(out, 0, pr->index + 1, len);
        else if (cu->offset == lastOffset && out->n && out->t[out->n - 1].kind == 0) toks_push(out, 1, 0, len);
        else { toks_push(out, 2, cu->offset, len); lastOffset = cu->offset; }
    }
    free(bl);
    return bits;
}

long optimize_ref(const uint8_t *in, int n, const Table *t, Toks *out) {
    static int ocost[MAX_OFFSET + 2];
    for (int o = 0; o <= MAX_OFFSET; o++) ocost[o] = INF;
    for (int k = 0; k < NB; k++) {
        long end = (long)t->base[k] + (1L << t->w[k]); if (end > MAX_OFFSET + 1) end = MAX_OFFSET + 1;
        for (long o = t->base[k]; o < end; o++) if (ocost[o] == INF) ocost[o] = CM_bit + CM_off * (4 + t->w[k]) + CM_nt;
    }
    int cover = 0; while (cover + 1 <= MAX_OFFSET && ocost[cover + 1] < INF) cover++;
    int size = ((n - 1 > 1 ? n - 1 : 1) < cover ? (n - 1 > 1 ? n - 1 : 1) : cover) + 1;
    int *lastLiteral = malloc(sizeof(int) * (size_t)size), *lastMatch = malloc(sizeof(int) * (size_t)size);
    int *matchLength = calloc((size_t)size, sizeof(int));
    int *optimal = malloc(sizeof(int) * (size_t)n);
    int *bestLength = calloc((size_t)n + 2, sizeof(int));
    for (int i = 0; i < size; i++) lastLiteral[i] = lastMatch[i] = -1;
    for (int i = 0; i < n; i++) optimal[i] = -1;
    Pn = 0; Pfree = -1;
    if (n > 2) bestLength[2] = 2;
    blk_assign(&lastMatch[1], blk_new(-CM_bit, -1, 1, -1));
    for (int index = 0; index < n; index++) {
        int maxOffset = index > 1 ? index : 1; if (maxOffset > cover) maxOffset = cover;
        int best = -1; int bestLengthSize = 2;
        const uint8_t c = in[index];
        for (int offset = 1; offset <= maxOffset; offset++) {
            if (index != 0 && index >= offset && c == in[index - offset]) {
                int lit = lastLiteral[offset];
                if (lit >= 0) {
                    int bits = P[lit].bits + CM_bit + GC(index - P[lit].index) + CM_rt + CM_mb * (index - P[lit].index);
                    int blk = blk_new(bits, index, offset, lit);
                    blk_assign(&lastMatch[offset], blk);
                    if (best < 0 || P[best].bits > bits) { blk_assign(&best, blk); }
                }
                if (++matchLength[offset] > 1) {
                    if (bestLengthSize < matchLength[offset]) {
                        int bits = P[optimal[index - bestLength[bestLengthSize]]].bits + GC(bestLength[bestLengthSize] - 1) + CM_mb * bestLength[bestLengthSize];
                        do {
                            bestLengthSize++;
                            int bits2 = P[optimal[index - bestLengthSize]].bits + GC(bestLengthSize - 1) + CM_mb * bestLengthSize;
                            if (bits2 <= bits) { bestLength[bestLengthSize] = bestLengthSize; bits = bits2; }
                            else bestLength[bestLengthSize] = bestLength[bestLengthSize - 1];
                        } while (bestLengthSize < matchLength[offset]);
                    }
                    int length = bestLength[matchLength[offset]];
                    int bits = P[optimal[index - length]].bits + ocost[offset] + GC(length - 1) + CM_mb * length;
                    int lm = lastMatch[offset];
                    if (lm < 0 || P[lm].index != index || P[lm].bits > bits) {
                        int blk = blk_new(bits, index, offset, optimal[index - length]);
                        blk_assign(&lastMatch[offset], blk);
                        if (best < 0 || P[best].bits > bits) blk_assign(&best, blk);
                    }
                }
            } else {
                matchLength[offset] = 0;
                int lm = lastMatch[offset];
                if (lm >= 0) {
                    int length = index - P[lm].index;
                    int bits = P[lm].bits + CM_bit + GC(length) + CM_lit * length + CM_lt;
                    int blk = blk_new(bits, index, 0, lm);
                    blk_assign(&lastLiteral[offset], blk);
                    if (best < 0 || P[best].bits > bits) blk_assign(&best, blk);
                }
            }
        }
        optimal[index] = best; /* takes over the reference held by best */
    }
    long bits = chain_to_toks(optimal[n - 1], out);
    free(lastLiteral); free(lastMatch); free(matchLength); free(optimal); free(bestLength);
    return bits;
}

/* ---- bit writer (ZX0 scheme: a bit byte is reserved where its first bit is written) ---- */
typedef struct { uint8_t *o; int n, pos, mask; } BW;
static inline void bw_bit(BW *w, int b) {
    if (!w->mask) { w->o[w->n] = 0; w->pos = w->n++; w->mask = 0x80; }
    if (b) w->o[w->pos] |= (uint8_t)w->mask;
    w->mask >>= 1;
}
static inline void bw_bits(BW *w, int v, int n) { for (int k = n - 1; k >= 0; k--) bw_bit(w, (v >> k) & 1); }
static inline void bw_gamma(BW *w, int v) {
    int nb = 32 - __builtin_clz((unsigned)v);
    for (int k = nb - 2; k >= 0; k--) { bw_bit(w, 0); bw_bit(w, (v >> k) & 1); }
    bw_bit(w, 1);
}
static int bucket_of(const Table *t, int o) {
    for (int k = 0; k < NB; k++) if (o >= t->base[k] && o - t->base[k] < (1 << t->w[k])) return k;
    fprintf(stderr, "offset not coded %d\n", o); exit(1);
}

int encode_toks(const uint8_t *in, const Toks *tk, const Table *t, uint8_t *out) {
    BW w = { out, 0, -1, 0 };
    for (int i = 0; i < tk->n; i++) {
        const Tok *x = &tk->t[i]; int nxt = i + 1 < tk->n ? tk->t[i + 1].kind : 3;
        if (x->kind == 0) {
            bw_gamma(&w, x->len); for (int j = 0; j < x->len; j++) out[w.n++] = in[x->a + j];
            bw_bit(&w, nxt == 1 ? 0 : 1);
        } else {
            if (x->kind == 1) bw_gamma(&w, x->len);
            else {
                int k = bucket_of(t, x->a);
                bw_bits(&w, k, 4); bw_bits(&w, x->a - t->base[k], t->w[k]); bw_gamma(&w, x->len - 1);
            }
            bw_bit(&w, nxt == 0 ? 0 : 1);
        }
    }
    bw_bits(&w, END_INDEX, 4);
    return w.n;
}

int decode_stream(const uint8_t *z, int zn, const Table *t, uint8_t *out, int cap) {
    int i = 0, mask = 0, cur = 0, on = 0, last = 1, state = 0;
#define BIT() (mask ? 0 : (i >= zn ? -1000000 : (cur = z[i++], mask = 0x80, 0)), r_ = (cur & mask) ? 1 : 0, mask >>= 1, r_)
    int r_;
    for (;;) {
        if (state == 0) {
            int v = 1; while (!BIT()) { v = v << 1 | BIT(); if (v > 65535) return -1; }
            if (i + v > zn || on + v > cap) return -1;
            for (int k = 0; k < v; k++) out[on++] = z[i++];
            state = BIT() ? 2 : 1;
        } else {
            int n;
            if (state == 1) { n = 1; while (!BIT()) { n = n << 1 | BIT(); if (n > 65535) return -1; } }
            else {
                int k = 0; for (int j = 0; j < 4; j++) k = k << 1 | BIT();
                if (k == END_INDEX) return on;
                int e = 0; for (int j = 0; j < t->w[k]; j++) e = e << 1 | BIT();
                last = t->base[k] + e;
                n = 1; while (!BIT()) { n = n << 1 | BIT(); if (n > 65535) return -1; } n++;
            }
            if (last > on || on + n > cap) return -1;
            for (int k = 0; k < n; k++) { out[on] = out[on - last]; on++; }
            state = BIT() ? 2 : 0;
        }
        if (i > zn) return -1;
    }
#undef BIT
}

/* ---- fitTable: DP over (bucket, base), ties to the smaller width (as in JS) ---- */
void fit_table(const double *h, Table *t) {
    static const int DEF[NB] = { 0, 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
    int top = 0; for (int o = 1; o <= MAX_OFFSET; o++) if (h[o] > 0) top = o;
    if (!top) { table_set(t, DEF); return; }
    double *pre = calloc((size_t)top + 2, sizeof(double));
    for (int o = 1; o <= top; o++) pre[o + 1] = pre[o] + h[o];
    /* cost[k][base] for base <= top; base > top costs 0; k == NB costs INF */
    double *cost = malloc(sizeof(double) * (size_t)(NB + 1) * (size_t)(top + 1));
    signed char *ch = malloc((size_t)(NB + 1) * (size_t)(top + 1));
#define C(k, b) cost[(size_t)(k) * (size_t)(top + 1) + (size_t)(b)]
#define CH(k, b) ch[(size_t)(k) * (size_t)(top + 1) + (size_t)(b)]
    for (int b = 1; b <= top; b++) { C(NB, b) = INF; CH(NB, b) = -1; }
    for (int k = NB - 1; k >= 0; k--) {
        for (int base = 1; base <= top; base++) {
            double r = INF; int rb = -1;
            for (int b = 0; b < 15; b++) {
                long nb = (long)base + (1L << b);
                double cnt = pre[nb < top + 1 ? nb : top + 1] - pre[base];
                double s = nb > top ? 0 : C(k + 1, nb);
                double c = s + cnt * (b + 4);
                if (c < r) { r = c; rb = b; }
                if (nb > top) break;
            }
            C(k, base) = r; CH(k, base) = (signed char)rb;
        }
    }
    int w[NB], m = 0; long base = 1;
    for (int k = 0; k < NB && base <= top; k++) { int b = CH(k, base); if (b < 0) break; w[m++] = b; base += 1L << b; }
    /* JS returns [] widths when the cost stays INF at some level; never happens with 15 buckets */
    while (m < NB) w[m++] = 14;
    table_set(t, w);
    free(pre); free(cost); free(ch);
#undef C
#undef CH
}

/* Fast parser: the same dynamic programming and the same tie rules as optimize_ref, but the
 * per-offset "last literal" and "last match" blocks are kept lazily (bits, index, chain) and are
 * only materialised as pool blocks when something chains to them. The chain values are the same,
 * hence the same tokens and streams (checked against optimize_ref and the JS encoder). */
long optimize_toks(const uint8_t *in, int n, const Table *t, Toks *out) {
    static int ocost[MAX_OFFSET + 2];
    for (int o = 0; o <= MAX_OFFSET; o++) ocost[o] = INF;
    for (int k = 0; k < NB; k++) {
        long end = (long)t->base[k] + (1L << t->w[k]); if (end > MAX_OFFSET + 1) end = MAX_OFFSET + 1;
        for (long o = t->base[k]; o < end; o++) if (ocost[o] == INF) ocost[o] = CM_bit + CM_off * (4 + t->w[k]) + CM_nt;
    }
    int cover = 0; while (cover + 1 <= MAX_OFFSET && ocost[cover + 1] < INF) cover++;
    int m1 = n - 1 > 1 ? n - 1 : 1; int size = (m1 < cover ? m1 : cover) + 1;
    typedef struct { int ml, lmIdx, lmBits, litIdx, litBits, lmChain, lmBlk, litBlk, litLm; } Off;
    Off *A = malloc(sizeof(Off) * (size_t)size);
    /* lmIdx == -2: no last match; lmChain == -2: chains to this offset's current literal */
    for (int i = 0; i < size; i++) { A[i].lmBlk = A[i].litLm = A[i].litBlk = A[i].litIdx = -1; A[i].lmIdx = -2; A[i].ml = 0; A[i].lmBits = A[i].litBits = A[i].lmChain = 0; }
    int *optimal = malloc(sizeof(int) * (size_t)n), *optBits = malloc(sizeof(int) * (size_t)n);
    int *bestLength = calloc((size_t)n + 2, sizeof(int));
    Pn = 0; Pfree = -1;
    if (n > 2) bestLength[2] = 2;
#define LIT_BLK(o) (A[o].litBlk >= 0 ? A[o].litBlk : (A[o].litBlk = blk_new(A[o].litBits, A[o].litIdx, 0, A[o].litLm), P[A[o].litBlk].refs++, A[o].litBlk))
#define LM_BLK(o) (A[o].lmBlk >= 0 ? A[o].lmBlk : (A[o].lmBlk = blk_new(A[o].lmBits, A[o].lmIdx, o, A[o].lmChain == -2 ? LIT_BLK(o) : A[o].lmChain), P[A[o].lmBlk].refs++, A[o].lmBlk))
#define SET_LM(o, b, i, ch) do { if (A[o].lmBlk >= 0) { blk_release(A[o].lmBlk); A[o].lmBlk = -1; } A[o].lmBits = (b); A[o].lmIdx = (i); A[o].lmChain = (ch); } while (0)
    { int b0 = blk_new(-CM_bit, -1, 1, -1); P[b0].refs++; A[1].lmBlk = b0; A[1].lmBits = -CM_bit; A[1].lmIdx = -1; A[1].lmChain = -1; }
    for (int index = 0; index < n; index++) {
        int maxOffset = index > 1 ? index : 1; if (maxOffset > cover) maxOffset = cover;
        int bestOff = 0, bestIsLit = 0, bestBits = 0; int have = 0; int bestLengthSize = 2;
        const uint8_t c = in[index];
        for (int offset = 1; offset <= maxOffset; offset++) {
            if (index != 0 && index >= offset && c == in[index - offset]) {
                if (A[offset].litIdx >= 0) {
                    int bits = A[offset].litBits + CM_bit + GC(index - A[offset].litIdx) + CM_rt + CM_mb * (index - A[offset].litIdx);
                    SET_LM(offset, bits, index, -2);
                    if (!have || bestBits > bits) { have = 1; bestBits = bits; bestOff = offset; bestIsLit = 0; }
                }
                if (++A[offset].ml > 1) {
                    int mlo = A[offset].ml;
                    if (bestLengthSize < mlo) {
                        int bits = optBits[index - bestLength[bestLengthSize]] + GC(bestLength[bestLengthSize] - 1) + CM_mb * bestLength[bestLengthSize];
                        do {
                            bestLengthSize++;
                            int bits2 = optBits[index - bestLengthSize] + GC(bestLengthSize - 1) + CM_mb * bestLengthSize;
                            if (bits2 <= bits) { bestLength[bestLengthSize] = bestLengthSize; bits = bits2; }
                            else bestLength[bestLengthSize] = bestLength[bestLengthSize - 1];
                        } while (bestLengthSize < mlo);
                    }
                    int length = bestLength[mlo];
                    int bits = optBits[index - length] + ocost[offset] + GC(length - 1) + CM_mb * length;
                    if (A[offset].lmIdx == -2 || A[offset].lmIdx != index || A[offset].lmBits > bits) {
                        SET_LM(offset, bits, index, optimal[index - length]);
                        if (!have || bestBits > bits) { have = 1; bestBits = bits; bestOff = offset; bestIsLit = 0; }
                    }
                }
            } else {
                A[offset].ml = 0;
                if (A[offset].lmIdx != -2) {
                    int length = index - A[offset].lmIdx;
                    int bits = A[offset].lmBits + CM_bit + GC(length) + CM_lit * length + CM_lt;
                    int m = LM_BLK(offset);
                    if (A[offset].litLm != m) { P[m].refs++; if (A[offset].litLm >= 0) blk_release(A[offset].litLm); A[offset].litLm = m; }
                    if (A[offset].litBlk >= 0) { blk_release(A[offset].litBlk); A[offset].litBlk = -1; }
                    A[offset].litIdx = index; A[offset].litBits = bits;
                    if (!have || bestBits > bits) { have = 1; bestBits = bits; bestOff = offset; bestIsLit = 1; }
                }
            }
        }
        int b = bestIsLit ? LIT_BLK(bestOff) : LM_BLK(bestOff);
        P[b].refs++; optimal[index] = b; optBits[index] = bestBits;
    }
#undef LIT_BLK
#undef LM_BLK
#undef SET_LM
    long bits = chain_to_toks(optimal[n - 1], out);
    free(A); free(optimal); free(optBits); free(bestLength);
    return bits;
}
