/* AMYZ encoder in C: optimal parser, table fit and table search (2026).
 * Format by Amy (AmyPurple), with Claude. Parser after ZX0's optimize.c (Einar Saukas),
 * adaptive buckets after Exomizer (Magnus Lind). Same format and decoders as the JS release
 * (see SPEC.md); for a given table the streams are byte-identical to js/amyz_core.js. */
#ifndef AMYZC_H
#define AMYZC_H
#include <stdint.h>
#include <stddef.h>

#define NB 15
#define END_INDEX 15
#define MAX_OFFSET 65535
/* speed-aware cost model: cost = CM_bit per bit, CM_lit per literal byte, CM_off per
 * index/extra bit, CM_lt/CM_rt/CM_nt per literal/repeat/new token, CM_mb per copied byte */
extern int CM_bit, CM_lit, CM_off, CM_lt, CM_rt, CM_nt, CM_mb;
#define INF 0x3fffffff

typedef struct { int w[NB]; int base[NB]; } Table;

/* tokens: kind 0 = literal (a = start, len), 1 = repeat (len), 2 = new (a = offset, len) */
typedef struct { uint8_t kind; int a; int len; } Tok;
typedef struct { Tok *t; int n, cap; } Toks;

void table_set(Table *t, const int *w);
void table_key(const Table *t, char *buf); /* "w0,w1,...", same as JS keyOf */

/* optimal parse of input under t; returns predicted stream bits (before end marker) */
long optimize_toks(const uint8_t *in, int n, const Table *t, Toks *out);
long optimize_ref(const uint8_t *in, int n, const Table *t, Toks *out); /* reference port */
/* encode tokens; returns stream length; out must hold n + n/4 + 64 bytes */
int encode_toks(const uint8_t *in, const Toks *tk, const Table *t, uint8_t *out);
/* decode; returns output length or -1 */
int decode_stream(const uint8_t *z, int zn, const Table *t, uint8_t *out, int cap);

/* optimal 15-bucket fit of an offset histogram (h[o], o = 1..MAX_OFFSET) */
void fit_table(const double *h, Table *t);

#endif
