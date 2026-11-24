/* amyzenc: AMYZ group encoder in C (table fit and table search), same format as the JS release.
 *
 * usage:
 *   amyzenc release CORPUS GROUPS WARRIOR OUT [GAME]       release fit (3 starts, histogram refit)
 *   amyzenc study   CORPUS GROUPS WARRIOR OUT GAME EVALS   port of the JS study search (fitGroupSearch)
 *   amyzenc search  CORPUS GROUPS WARRIOR OUT GAME SECONDS [STARTDIR]
 *                                                          wide search (this work), time-bounded
 *   amyzenc encode  CORPUS GROUPS WARRIOR OUT TABLEDIR [GAME]  encode with given .tabz tables
 * CORPUS: compress/corpus; GROUPS: groups.json; WARRIOR: the warrior directory (color.bin, pattern.bin).
 * OUT receives <cat>/<file> streams, tables/<game>.tabz/.tabzx and files.<game>.tsv.
 * STARTDIR (search): .tabz tables of OTHER games used as extra starts (the game's own is skipped).
 */
#include "amyzc.h"
#include <math.h>
static int SPEED = 0;   /* -speed N, see speed_setup() */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>

typedef struct { char rel[256]; char cat[64]; char name[192]; uint8_t *d; int n; } File;
typedef struct { char name[64]; File *f; int nf; } Game;
static Game G[64]; static int NG;

static double now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }

static uint8_t *load(const char *p, int *n) {
    FILE *f = fopen(p, "rb"); if (!f) { perror(p); exit(1); }
    fseek(f, 0, SEEK_END); long s = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)s + 1); if (fread(b, 1, (size_t)s, f) != (size_t)s) { perror(p); exit(1); }
    fclose(f); *n = (int)s; return b;
}
static void mkdirs(const char *p) {
    char b[1024]; snprintf(b, sizeof b, "%s", p);
    for (char *q = b + 1; *q; q++) if (*q == '/') { *q = 0; mkdir(b, 0777); *q = '/'; }
    mkdir(b, 0777);
}
static Game *game_get(const char *name) {
    for (int i = 0; i < NG; i++) if (!strcmp(G[i].name, name)) return &G[i];
    snprintf(G[NG].name, sizeof G[NG].name, "%s", name); return &G[NG++];
}
static void add_file(Game *g, const char *root, const char *rel) {
    g->f = realloc(g->f, sizeof(File) * (size_t)(g->nf + 1)); File *f = &g->f[g->nf++]; memset(f, 0, sizeof *f);
    snprintf(f->rel, sizeof f->rel, "%s", rel);
    const char *sl = strrchr(rel, '/'); snprintf(f->cat, sizeof f->cat, "%.*s", (int)(sl - rel), rel); snprintf(f->name, sizeof f->name, "%s", sl + 1);
    char p[1024]; snprintf(p, sizeof p, "%s/%s", root, rel); f->d = load(p, &f->n);
}
static int cmp_file(const void *a, const void *b) { return strcmp(((const File *)a)->rel, ((const File *)b)->rel); }
static int cmp_game(const void *a, const void *b) { return strcmp(((const Game *)a)->name, ((const Game *)b)->name); }

/* same grouping as the study's common.mjs: game = group name up to the first space */
static void load_corpus(const char *corpus, const char *groups, const char *warrior) {
    int n; char *js = (char *)load(groups, &n); js[n] = 0;
    char *p = js;
    for (;;) {
        char *a = strchr(p, '"'); if (!a) break; char *b = strchr(a + 1, '"'); char *c = strchr(b + 1, '"'); char *d = strchr(c + 1, '"');
        *b = 0; *d = 0; char gname[64]; snprintf(gname, sizeof gname, "%s", c + 1); char *sp = strchr(gname, ' '); if (sp) *sp = 0;
        add_file(game_get(gname), corpus, a + 1); p = d + 1;
    }
    free(js);
    Game *w = game_get("warrior"); DIR *dir = opendir(warrior); struct dirent *e; char parent[1024]; snprintf(parent, sizeof parent, "%s/..", warrior);
    while ((e = readdir(dir))) { size_t l = strlen(e->d_name); if (l > 4 && !strcmp(e->d_name + l - 4, ".bin")) { char rel[300]; snprintf(rel, sizeof rel, "warrior/%s", e->d_name); add_file(w, parent, rel); } }
    closedir(dir);
    qsort(G, (size_t)NG, sizeof(Game), cmp_game);
    for (int i = 0; i < NG; i++) qsort(G[i].f, (size_t)G[i].nf, sizeof(File), cmp_file);
}

/* ---------------- evaluation (full parse of a game) with a cache ---------------- */
typedef struct { Table t; long size; Toks *tk; long predicted; } Res; /* tk: one Toks per file, or NULL */
typedef struct { char key[64]; long size; Toks *tk; } CEnt;
static CEnt *cache; static int ncache, capcache; static long cache_bytes; static long budget = 1L << 40;
static long n_parses; static double t_parse;
static const long CACHE_MAX = 600L << 20;
static Game *CUR;

static int stream_cap(int n) { return n + n / 4 + 64; }

static Toks *parse_all(const Table *t, long *size, long *pred) {
    Toks *tk = calloc((size_t)CUR->nf, sizeof(Toks)); long s = 0, pr = 0; uint8_t *buf = NULL; int cap = 0;
    double t0 = now();
    for (int i = 0; i < CUR->nf; i++) {
        File *f = &CUR->f[i]; if (stream_cap(f->n) > cap) { cap = stream_cap(f->n); buf = realloc(buf, (size_t)cap); }
        long bits = optimize_toks(f->d, f->n, t, &tk[i]);
        int zn = encode_toks(f->d, &tk[i], t, buf); s += zn; pr += (bits + 5 + 7) / 8;
    }
    free(buf); t_parse += now() - t0; n_parses++;
    *size = s; if (pred) *pred = pr; return tk;
}
static void toks_free(Toks *tk) { if (!tk) return; for (int i = 0; i < CUR->nf; i++) free(tk[i].t); free(tk); }
static long toks_bytes(Toks *tk) { long b = 0; for (int i = 0; i < CUR->nf; i++) b += (long)tk[i].n * (long)sizeof(Tok); return b; }
static CEnt *cache_find(const char *k) { for (int i = 0; i < ncache; i++) if (!strcmp(cache[i].key, k)) return &cache[i]; return NULL; }
static void cache_reset(void) { for (int i = 0; i < ncache; i++) toks_free(cache[i].tk); ncache = 0; cache_bytes = 0; }

static int valid_w(const int *w) {
    for (int k = 0; k < NB; k++) if (w[k] < 0 || w[k] > 14) return 0;
    return 1;
}
/* ev: returns 1 and fills r (r->tk borrowed from cache or freshly parsed and owned by cache) */
static int ev(const Table *t, Res *r) {
    char k[64]; table_key(t, k); CEnt *c = cache_find(k);
    if (c) {
        r->t = *t; r->size = c->size;
        if (!c->tk) { long s; c->tk = parse_all(t, &s, NULL); n_parses--; cache_bytes += toks_bytes(c->tk); }
        r->tk = c->tk; return 1;
    }
    if (budget <= 0) return 0;
    budget--;
    long s, pr; Toks *tk = parse_all(t, &s, &pr);
    if (pr != s) { fprintf(stderr, "COST MODEL MISMATCH %s predicted %ld actual %ld\n", k, pr, s); exit(2); }
    if (ncache == capcache) { capcache = capcache ? capcache * 2 : 256; cache = realloc(cache, sizeof(CEnt) * (size_t)capcache); }
    c = &cache[ncache++]; snprintf(c->key, sizeof c->key, "%s", k); c->size = s; c->tk = tk; cache_bytes += toks_bytes(tk);
    /* drop token lists of other entries when the cache is too big (they are re-parsed on demand) */
    for (int i = 0; i < ncache - 1 && cache_bytes > CACHE_MAX; i++) if (cache[i].tk) { cache_bytes -= toks_bytes(cache[i].tk); toks_free(cache[i].tk); cache[i].tk = NULL; }
    r->t = *t; r->size = s; r->tk = tk; return 1;
}
/* keep tokens of a result alive outside the cache */
static Toks *toks_copy(Toks *tk) {
    Toks *c = calloc((size_t)CUR->nf, sizeof(Toks));
    for (int i = 0; i < CUR->nf; i++) { c[i].n = c[i].cap = tk[i].n; c[i].t = malloc(sizeof(Tok) * (size_t)(tk[i].n + 1)); memcpy(c[i].t, tk[i].t, sizeof(Tok) * (size_t)tk[i].n); }
    return c;
}

/* ---------------- refits ---------------- */
static void hist_refit(Toks *tk, Table *out) {
    static double h[MAX_OFFSET + 1]; memset(h, 0, sizeof h);
    for (int i = 0; i < CUR->nf; i++) for (int j = 0; j < tk[i].n; j++) if (tk[i].t[j].kind == 2) h[tk[i].t[j].a] += 1;
    fit_table(h, out);
}

/* alternatives: for each new-offset token, every offset that reproduces it (and the repeat that
 * follows after one literal run). Flattened: tok i has offsets alt[st[i] .. st[i+1]-1], cur[i]. */
typedef struct { int n; int *st; int *alt; int *cur; int na, capa; } Alts;
static void alts_free(Alts *a) { free(a->st); free(a->alt); free(a->cur); memset(a, 0, sizeof *a); }
static void alts_build(Toks *tkall, Alts *A) {
    memset(A, 0, sizeof *A); int capn = 1024; A->st = malloc(sizeof(int) * (size_t)(capn + 1)); A->cur = malloc(sizeof(int) * (size_t)capn);
    A->capa = 1 << 16; A->alt = malloc(sizeof(int) * (size_t)A->capa); A->st[0] = 0;
    static int head[65536];
    for (int fi = 0; fi < CUR->nf; fi++) {
        const uint8_t *d = CUR->f[fi].d; int n = CUR->f[fi].n; Toks *tk = &tkall[fi];
        int *prv = malloc(sizeof(int) * (size_t)(n + 1)); for (int k = 0; k < 65536; k++) head[k] = -1;
        int *pos = malloc(sizeof(int) * (size_t)(tk->n + 1)); int p = 0;
        for (int i = 0; i < tk->n; i++) { pos[i] = p; p += tk->t[i].len; }
        int next = 0;
        for (int i = 0; i < tk->n; i++) {
            Tok *x = &tk->t[i]; if (x->kind != 2) continue;
            int p0 = pos[i], L = x->len;
            while (next < p0) { if (next + 1 < n) { int key = d[next] << 8 | d[next + 1]; prv[next] = head[key]; head[key] = next; } next++; }
            int rp = -1, rl = 0;
            if (i + 2 < tk->n && tk->t[i + 1].kind == 0 && tk->t[i + 2].kind == 1) { rp = pos[i + 2]; rl = tk->t[i + 2].len; }
            if (A->n == capn) { capn *= 2; A->st = realloc(A->st, sizeof(int) * (size_t)(capn + 1)); A->cur = realloc(A->cur, sizeof(int) * (size_t)capn); }
            int has = 0;
            for (int j = head[d[p0] << 8 | d[p0 + 1]]; j >= 0; j = prv[j]) {
                int o = p0 - j; if (o > MAX_OFFSET) break;
                int ok = 1; for (int m = 2; m < L; m++) if (d[p0 + m] != d[p0 + m - o]) { ok = 0; break; }
                if (ok && rp >= 0) for (int m = 0; m < rl; m++) if (d[rp + m] != d[rp + m - o]) { ok = 0; break; }
                if (ok) { if (A->na == A->capa) { A->capa *= 2; A->alt = realloc(A->alt, sizeof(int) * (size_t)A->capa); } A->alt[A->na++] = o; if (o == x->a) has = 1; }
            }
            if (!has) { if (A->na == A->capa) { A->capa *= 2; A->alt = realloc(A->alt, sizeof(int) * (size_t)A->capa); } A->alt[A->na++] = x->a; }
            A->cur[A->n] = x->a; A->n++; A->st[A->n] = A->na;
        }
        free(prv); free(pos);
    }
}
static void width_costs(const Table *t, int *c) {
    for (int o = 0; o <= MAX_OFFSET; o++) c[o] = INF;
    for (int k = 0; k < NB; k++) {
        long e = (long)t->base[k] + (1L << t->w[k]); if (e > MAX_OFFSET + 1) e = MAX_OFFSET + 1;
        for (long o = t->base[k]; o < e; o++) if (c[o] == INF) c[o] = 4 + t->w[k];
    }
}
static int em_fit(Alts *A, int iters, Table *out) {
    int *asg = malloc(sizeof(int) * (size_t)(A->n + 1)); memcpy(asg, A->cur, sizeof(int) * (size_t)A->n);
    static double h[MAX_OFFSET + 1]; static int c[MAX_OFFSET + 1]; int got = 0; double prev = 1e300;
    for (int it = 0; it < iters; it++) {
        memset(h, 0, sizeof h); for (int i = 0; i < A->n; i++) h[asg[i]] += 1;
        Table nt; fit_table(h, &nt); width_costs(&nt, c); int changed = 0; double obj = 0;
        for (int i = 0; i < A->n; i++) {
            int b = asg[i];
            for (int j = A->st[i]; j < A->st[i + 1]; j++) { int o = A->alt[j]; if (c[o] < c[b]) b = o; }
            if (b != asg[i]) { asg[i] = b; changed++; }
            obj += c[b];
        }
        if (obj > prev) break;
        *out = nt; got = 1; prev = obj; if (!changed) break;
    }
    free(asg); return got;
}
static long surrogate(Alts *A, const Table *t) {
    static int c[MAX_OFFSET + 1]; width_costs(t, c); long s = 0;
    for (int i = 0; i < A->n; i++) { int m = INF; for (int j = A->st[i]; j < A->st[i + 1]; j++) if (c[A->alt[j]] < m) m = c[A->alt[j]]; s += m; }
    return s;
}

/* fitLoopCached of the study; mode 0 = histogram refit, 1 = EM refit. Returns 0 if no result. */
static int fit_loop(const Table *start, int mode, int passes, Res *best) {
    Res r; if (!ev(start, &r)) return 0;
    Toks *keep = toks_copy(r.tk); *best = r; best->tk = keep;
    for (int p = 1; p < passes; p++) {
        Table c;
        if (mode == 1) { Alts A; alts_build(best->tk, &A); int ok = em_fit(&A, 12, &c); alts_free(&A); if (!ok) break; }
        else hist_refit(best->tk, &c);
        Res e; if (!ev(&c, &e)) break;
        if (e.size >= best->size) break;
        toks_free(best->tk); *best = e; best->tk = toks_copy(e.tk);
    }
    return 1;
}
static void res_take(Res *best, Res *r) { /* best = r if smaller; frees the loser's tokens */
    if (!best->tk || r->size < best->size) { toks_free(best->tk); *best = *r; } else toks_free(r->tk);
}

static const int STARTS[3][NB] = {
    { 0, 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 },
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 },
    { 0, 2, 1, 0, 3, 4, 4, 5, 5, 7, 3, 3, 8, 10, 13 },
};
static const int EXTRA[10][NB] = {
    { 0, 2, 1, 0, 3, 3, 3, 3, 3, 4, 4, 5, 4, 7, 9 }, { 0, 1, 1, 1, 0, 2, 2, 4, 4, 4, 2, 5, 5, 7, 9 },
    { 0, 1, 2, 0, 2, 2, 3, 2, 2, 3, 3, 4, 5, 6, 9 }, { 0, 1, 2, 0, 1, 2, 1, 3, 3, 4, 4, 6, 7, 6, 9 },
    { 0, 2, 1, 0, 2, 2, 2, 2, 3, 4, 4, 6, 6, 6, 9 }, { 0, 2, 1, 0, 1, 2, 0, 4, 0, 4, 4, 3, 5, 6, 8 },
    { 0, 1, 2, 0, 1, 2, 3, 3, 1, 4, 4, 5, 5, 7, 10 }, { 0, 1, 2, 2, 2, 0, 3, 3, 0, 4, 5, 4, 6, 5, 10 },
    { 0, 2, 1, 0, 1, 2, 1, 3, 4, 4, 4, 6, 7, 8, 10 }, { 0, 3, 5, 6, 7, 4, 1, 2, 0, 4, 7, 7, 9, 9, 12 },
};

static int release_fit(Res *best) {
    memset(best, 0, sizeof *best);
    for (int s = 0; s < 3; s++) { Table t; table_set(&t, STARTS[s]); Res r; fit_loop(&t, 0, 8, &r); res_take(best, &r); }
    return 1;
}

/* neighbour generation shared by the study port and the wide search */
typedef struct { int w[NB]; long sur; char key[64]; } Cand;
static Cand *cands; static int ncand, capcand;
static void cand_add(const int *w) {
    if (!valid_w(w)) return;
    Table t; table_set(&t, w); char k[64]; table_key(&t, k);
    if (cache_find(k)) return;
    for (int i = 0; i < ncand; i++) if (!strcmp(cands[i].key, k)) return;
    if (ncand == capcand) { capcand = capcand ? capcand * 2 : 1024; cands = realloc(cands, sizeof(Cand) * (size_t)capcand); }
    memcpy(cands[ncand].w, w, sizeof(int) * NB); snprintf(cands[ncand].key, 64, "%s", k); ncand++;
}
static int cmp_cand(const void *a, const void *b) {
    const Cand *x = a, *y = b; if (x->sur != y->sur) return x->sur < y->sur ? -1 : 1; return strcmp(x->key, y->key);
}
/* study neighbourhood: +-1 single, +-1 pair, remove a bucket, insert a bucket */
static void neigh_study(const int *w0) {
    int w[NB], v[NB];
    for (int i = 0; i < NB; i++) for (int di = -1; di <= 1; di += 2) {
        memcpy(w, w0, sizeof w); w[i] += di; cand_add(w);
        for (int j = 0; j < NB; j++) if (j != i) { memcpy(v, w, sizeof v); v[j] -= di; cand_add(v); }
    }
    for (int i = 0; i < NB; i++) {
        int m = 0; for (int k = 0; k < NB; k++) if (k != i) w[m++] = w0[k]; w[m] = 14; cand_add(w);
        for (int b = 0; b <= 14; b++) { m = 0; for (int k = 0; k < NB - 1; k++) { if (k == i) v[m++] = b; v[m++] = w0[k]; } if (i == NB - 1) v[m++] = b; cand_add(v); }
    }
}
/* wide neighbourhood: study moves plus +-2,+-3 single, +-2 pair, merge two equal buckets,
 * split one, swap two adjacent widths */
static void neigh_wide(const int *w0) {
    neigh_study(w0);
    int w[NB], v[NB];
    for (int i = 0; i < NB; i++) for (int d = -3; d <= 3; d++) {
        if (d == 0) continue;
        memcpy(w, w0, sizeof w); w[i] += d; if (d < -1 || d > 1) cand_add(w);
        if (d == 2 || d == -2) for (int j = 0; j < NB; j++) if (j != i) { memcpy(v, w, sizeof v); v[j] -= d; cand_add(v); }
    }
    for (int i = 0; i + 1 < NB; i++) {
        memcpy(w, w0, sizeof w); int x = w[i]; w[i] = w[i + 1]; w[i + 1] = x; cand_add(w);
        if (w0[i] == w0[i + 1] && w0[i] < 14) { int m = 0; for (int k = 0; k < NB; k++) { if (k == i + 1) continue; v[m++] = k == i ? w0[i] + 1 : w0[k]; } v[m] = 14; cand_add(v); }
        if (w0[i] > 0) { int m = 0; for (int k = 0; k < NB && m < NB; k++) { if (k == i) { v[m++] = w0[i] - 1; if (m < NB) v[m++] = w0[i] - 1; } else v[m++] = w0[k]; } cand_add(v); }
    }
}

static void study_search(int maxEvals, Res *best) {
    budget = 1L << 40; release_fit(best); long release = best->size;
    budget = maxEvals;
    for (int s = 0; s < 10; s++) { Table t; table_set(&t, EXTRA[s]); Res r; if (fit_loop(&t, 1, 8, &r)) { if (r.size < best->size) res_take(best, &r); else toks_free(r.tk); } }
    for (int round = 0; round < 6 && budget > 0; round++) {
        Alts A; alts_build(best->tk, &A); ncand = 0; neigh_study(best->t.w);
        for (int i = 0; i < ncand; i++) { Table t; table_set(&t, cands[i].w); cands[i].sur = surrogate(&A, &t); }
        alts_free(&A); qsort(cands, (size_t)ncand, sizeof(Cand), cmp_cand);
        int improved = 0; int nc = ncand < 4 ? ncand : 4; Cand top[4]; memcpy(top, cands, sizeof(Cand) * (size_t)nc);
        for (int i = 0; i < nc; i++) { Table t; table_set(&t, top[i].w); Res e; if (ev(&t, &e) && e.size < best->size) { toks_free(best->tk); *best = e; best->tk = toks_copy(e.tk); improved = 1; } }
        if (!improved) break;
        Res f; if (fit_loop(&best->t, 1, 4, &f)) { if (f.size < best->size) res_take(best, &f); else toks_free(f.tk); }
    }
    fprintf(stderr, "  study: release %ld -> %ld\n", release, best->size);
}

/* ---------------- wide search ---------------- */
static double T0, TLIM; static FILE *LOG;
static long BESTSZ;
static void note(Res *best, const char *what) {
    if (best->size < BESTSZ) {
        BESTSZ = best->size; char k[64]; table_key(&best->t, k);
        fprintf(LOG, "%8.2f s  %6ld parses  size %ld  %s  [%s]\n", now() - T0, n_parses, best->size, what, k); fflush(LOG);
    }
}
static int timeup(void) { return now() - T0 > TLIM; }
static void try_start(const int *w, Res *best, const char *what) {
    if (!valid_w(w) || timeup()) return;
    Table t; table_set(&t, w);
    for (int mode = 0; mode < 2 && !timeup(); mode++) {
        Res r; if (!fit_loop(&t, mode, 8, &r)) continue;
        if (r.size < best->size) { res_take(best, &r); note(best, what); } else toks_free(r.tk);
    }
}
/* synthetic starts: fits of power-law offset histograms h(o) = o^-a over 1..top */
static void synth_starts(Res *best, int maxoff) {
    static double h[MAX_OFFSET + 1];
    static const double A[] = { 0.0, 0.5, 0.8, 1.0, 1.2, 1.5, 2.0 };
    for (unsigned ai = 0; ai < sizeof A / sizeof A[0]; ai++) {
        memset(h, 0, sizeof h); for (int o = 1; o <= maxoff; o++) { h[o] = 1e6 * 1.0 / __builtin_pow(o, A[ai]); }
        Table t; fit_table(h, &t); try_start(t.w, best, "synthetic power-law start");
    }
}
/* local search: rank the wide neighbourhood with the surrogate, parse the best K, move to the
 * best improvement, polish with an EM refit; stop on no improvement or time */
static void local_search(Res *best, int K) {
    for (;;) {
        if (timeup()) return;
        Alts A; alts_build(best->tk, &A); ncand = 0; neigh_wide(best->t.w);
        for (int i = 0; i < ncand; i++) { Table t; table_set(&t, cands[i].w); cands[i].sur = surrogate(&A, &t); }
        alts_free(&A); qsort(cands, (size_t)ncand, sizeof(Cand), cmp_cand);
        int nc = ncand < K ? ncand : K; Cand *top = malloc(sizeof(Cand) * (size_t)(nc + 1)); memcpy(top, cands, sizeof(Cand) * (size_t)nc);
        Res bestn; memset(&bestn, 0, sizeof bestn); bestn.size = best->size;
        for (int i = 0; i < nc && !timeup(); i++) {
            Table t; table_set(&t, top[i].w); Res e;
            if (ev(&t, &e) && e.size < bestn.size) { toks_free(bestn.tk); bestn = e; bestn.tk = toks_copy(e.tk); }
        }
        free(top);
        if (!bestn.tk) return;
        toks_free(best->tk); *best = bestn; note(best, "local move");
        Res f; if (fit_loop(&best->t, 1, 4, &f)) { if (f.size < best->size) { res_take(best, &f); note(best, "EM polish"); } else toks_free(f.tk); }
    }
}
/* perturbation (iterated local search): deterministic LCG, random multi-move kicks from the best */
static unsigned long long rng = 0x9E3779B97F4A7C15ULL;
static int rnd(int n) { rng = rng * 6364136223846793005ULL + 1442695040888963407ULL; return (int)((rng >> 33) % (unsigned long long)n); }
static void kick_search(Res *best, int K) {
    while (!timeup()) {
        int w[NB]; memcpy(w, best->t.w, sizeof w);
        int moves = 2 + rnd(3);
        for (int m = 0; m < moves; m++) {
            int i = rnd(NB), d = rnd(2) ? 1 : -1;
            if (rnd(2)) { int j = rnd(NB); w[i] += d; w[j] -= d; } else w[i] += d * (1 + rnd(2));
        }
        if (!valid_w(w)) continue;
        Table t; table_set(&t, w); Res r; if (!ev(&t, &r)) return;
        Res cur = r; cur.tk = toks_copy(r.tk);
        /* descend from the kicked table with a short local search */
        Res f; if (fit_loop(&cur.t, 1, 4, &f)) { if (f.size < cur.size) { toks_free(cur.tk); cur = f; } else toks_free(f.tk); }
        local_search(&cur, K);
        if (cur.size < best->size) { toks_free(best->tk); *best = cur; note(best, "kick + local search"); } else toks_free(cur.tk);
    }
}

static int read_tabz(const char *p, int *w) {
    FILE *f = fopen(p, "rb"); if (!f) return 0; uint8_t b[15]; int ok = fread(b, 1, 15, f) == 15; fclose(f);
    if (ok) for (int k = 0; k < NB; k++) w[k] = b[k] - 1;
    return ok;
}

static void wide_search(double seconds, const char *startdir, Res *best) {
    T0 = now(); TLIM = seconds; BESTSZ = 1L << 40; budget = 1L << 40;
    const char *seed = getenv("AMYZ_SEED"); int K = getenv("AMYZ_K") ? atoi(getenv("AMYZ_K")) : 24;
    long release;
    if (seed) { /* resume: start from a table found by an earlier run (searches longer than one command) */
        int w[NB]; if (!read_tabz(seed, w)) { fprintf(stderr, "no seed %s\n", seed); exit(1); }
        memset(best, 0, sizeof *best); Table t; table_set(&t, w); fit_loop(&t, 1, 4, best); note(best, "seed");
        release = best->size; goto local;
    }
    release_fit(best); note(best, "release fit (3 starts)");
    release = best->size;
    if (startdir) {
        DIR *d = opendir(startdir); struct dirent *e;
        char names[64][256]; int nn = 0;
        while (d && (e = readdir(d)) && nn < 64) { size_t l = strlen(e->d_name); if (l > 5 && !strcmp(e->d_name + l - 5, ".tabz")) snprintf(names[nn++], 256, "%s", e->d_name); }
        if (d) closedir(d);
        qsort(names, (size_t)nn, 256, (int (*)(const void *, const void *))strcmp);
        for (int i = 0; i < nn && !timeup(); i++) {
            char own[128]; snprintf(own, sizeof own, "%s.tabz", CUR->name); if (!strcmp(names[i], own)) continue;
            char p[1024]; snprintf(p, sizeof p, "%s/%s", startdir, names[i]); int w[NB];
            if (read_tabz(p, w)) { char what[300]; snprintf(what, sizeof what, "start from other game %s", names[i]); try_start(w, best, what); }
        }
    }
    int maxoff = 0; for (int i = 0; i < CUR->nf; i++) if (CUR->f[i].n - 1 > maxoff) maxoff = CUR->f[i].n - 1; if (maxoff > MAX_OFFSET) maxoff = MAX_OFFSET;
    synth_starts(best, maxoff);
    /* uniform-ish starts */
    for (int c = 1; c <= 8 && !timeup(); c++) {
        int w[NB]; for (int k = 0; k < NB; k++) w[k] = k < 2 ? k : (k < 11 ? c : c + 2 * (k - 10)); if (w[14] < 9) w[14] = 9;
        for (int k = 0; k < NB; k++) if (w[k] > 14) w[k] = 14;
        try_start(w, best, "uniform start");
    }
    fprintf(LOG, "starts done: %ld (release %ld) at %.1f s\n", best->size, release, now() - T0);
local:
    local_search(best, K);
    fprintf(LOG, "local search done: %ld at %.1f s\n", best->size, now() - T0);
    kick_search(best, K / 2 > 4 ? K / 2 : 4);
    fprintf(LOG, "final %ld (release %ld, %+ld) in %.1f s, %ld parses (%.3f s each)\n", best->size, release, best->size - release, now() - T0, n_parses, t_parse / (double)(n_parses ? n_parses : 1));
}

/* ---------------- output and verification ---------------- */
static int write_game(const char *out, Res *best) {
    char p[1024]; int fails = 0; long total = 0;
    snprintf(p, sizeof p, "%s/tables", out); mkdirs(p);
    uint8_t t15[15], tx[45]; long base = 1;
    for (int k = 0; k < NB; k++) { t15[k] = (uint8_t)(best->t.w[k] + 1); long v = (1 - base) & 0xffff; tx[3 * k] = v & 255; tx[3 * k + 1] = (uint8_t)(v >> 8); tx[3 * k + 2] = (uint8_t)best->t.w[k]; base += 1L << best->t.w[k]; }
    snprintf(p, sizeof p, "%s/tables/%s.tabz", out, CUR->name); FILE *f = fopen(p, "wb"); fwrite(t15, 1, 15, f); fclose(f);
    snprintf(p, sizeof p, "%s/tables/%s.tabzx", out, CUR->name); f = fopen(p, "wb"); fwrite(tx, 1, 45, f); fclose(f);
    snprintf(p, sizeof p, "%s/files.%s.tsv", out, CUR->name); FILE *tsv = fopen(p, "w");
    for (int i = 0; i < CUR->nf; i++) {
        File *fl = &CUR->f[i]; uint8_t *z = malloc((size_t)stream_cap(fl->n)); Toks tk = { 0 };
        long bits = optimize_toks(fl->d, fl->n, &best->t, &tk); int zn = encode_toks(fl->d, &tk, &best->t, z);
        if (!SPEED && (bits + 5 + 7) / 8 != zn) { fprintf(stderr, "cost model mismatch %s\n", fl->rel); fails++; }
        uint8_t *o = malloc((size_t)fl->n + 16); int on = decode_stream(z, zn, &best->t, o, fl->n + 16);
        if (on != fl->n || memcmp(o, fl->d, (size_t)fl->n)) { fprintf(stderr, "ROUND TRIP FAIL %s\n", fl->rel); fails++; }
        snprintf(p, sizeof p, "%s/%s", out, fl->cat); mkdirs(p);
        snprintf(p, sizeof p, "%s/%s/%s", out, fl->cat, fl->name); f = fopen(p, "wb"); fwrite(z, 1, (size_t)zn, f); fclose(f);
        fprintf(tsv, "%s\t%s\t%d\t%s\n", fl->cat, fl->name, zn, CUR->name); total += zn;
        free(z); free(o); free(tk.t);
    }
    fclose(tsv);
    char k[64]; table_key(&best->t, k);
    printf("%s\tsize %ld\twidths %s\tfails %d\n", CUR->name, total, k, fails); fflush(stdout);
    return fails;
}

/* -speed N (environment AMYZ_SPEED=N, AMYZ_CPU=6502|z80; encode mode only): cost =
 * 1000 * bits + N * decoder cycles, with the per-token cycle costs measured by
 * speed/fit_model.mjs (same numbers as speed/model_*.json, so C and JS parse alike). */
static void speed_setup(void) {
    const char *e = getenv("AMYZ_SPEED"); if (!e || !atoi(e)) return;
    SPEED = atoi(e); const char *cpu = getenv("AMYZ_CPU");
    static const double M6502[7] = { 16.146602410403425, 16.70365533487417, 21.28632954891679, 72.34597965888155, 64.46876770573567, 114.28124321063103, 17.0375687145763 }, MZ80[7] = { 33.86476888189642, 33.8531143741415, 49.778908215468405, 112.37863289997985, 170.50399412983265, 407.29177625643547, 115.08347008031588 };
    const double *c = (cpu && !strcmp(cpu, "z80")) ? MZ80 : M6502;
    double N = SPEED;
    CM_bit = (int)lround(1000 + N * c[0]); CM_lit = (int)lround(8000 + N * c[1]); CM_off = (int)lround(1000 + N * c[2]);
    CM_lt = (int)lround(N * c[3]); CM_rt = (int)lround(N * c[4]); CM_nt = (int)lround(N * c[5]); CM_mb = (int)lround(N * c[6]);
    fprintf(stderr, "speed %d (%s): bit %d lit %d off %d litTok %d repTok %d newTok %d matchByte %d\n", SPEED, cpu ? cpu : "6502", CM_bit, CM_lit, CM_off, CM_lt, CM_rt, CM_nt, CM_mb);
}

int main(int argc, char **argv) {
    speed_setup();
    if (argc < 6) { fprintf(stderr, "see the header of amyzenc.c for usage\n"); return 1; }
    const char *mode = argv[1];
    if (SPEED && strcmp(mode, "encode")) { fprintf(stderr, "AMYZ_SPEED needs encode mode (a given table)\n"); return 1; }
    load_corpus(argv[2], argv[3], argv[4]); const char *out = argv[5]; mkdirs(out);
    const char *only = NULL; int fails = 0; long total = 0;
    if (!strcmp(mode, "release") || !strcmp(mode, "encode")) only = argc > (strcmp(mode, "encode") ? 6 : 7) ? argv[strcmp(mode, "encode") ? 6 : 7] : NULL;
    else only = argv[6];
    LOG = stderr;
    for (int gi = 0; gi < NG; gi++) {
        CUR = &G[gi]; if (only && strcmp(only, CUR->name)) continue;
        n_parses = 0; t_parse = 0; double t0 = now(); Res best; memset(&best, 0, sizeof best);
        if (!strcmp(mode, "release")) { budget = 1L << 40; release_fit(&best); }
        else if (!strcmp(mode, "study")) study_search(atoi(argv[7]), &best);
        else if (!strcmp(mode, "search")) wide_search(atof(argv[7]), argc > 8 ? argv[8] : NULL, &best);
        else if (!strcmp(mode, "encode")) {
            char p[1024]; snprintf(p, sizeof p, "%s/%s.tabz", argv[6], CUR->name); int w[NB];
            if (!read_tabz(p, w)) { fprintf(stderr, "no table %s\n", p); return 1; }
            table_set(&best.t, w);
        } else { fprintf(stderr, "unknown mode %s\n", mode); return 1; }
        fails += write_game(out, &best);
        toks_free(best.tk); cache_reset();
        fprintf(stderr, "  %s: %.2f s, %ld parses\n", CUR->name, now() - t0, n_parses);
        char p[1024]; snprintf(p, sizeof p, "%s/files.%s.tsv", out, CUR->name);
        FILE *f = fopen(p, "r"); char line[1024]; while (fgets(line, sizeof line, f)) { char *a = strchr(line, '\t'); a = strchr(a + 1, '\t'); total += atol(a + 1); } fclose(f);
    }
    printf("TOTAL %ld  fails %d\n", total, fails);
    return fails ? 3 : 0;
}
