/* sm83.h - small Sharp SM83 (LR35902, Game Boy CPU) interpreter for test
   harnesses. Licence: CC0. Flat memory through rd/wr callbacks, no
   interrupts, no timers; HALT and STOP and undefined opcodes stop the run.
   Cycle counts are T-states (4 per M-cycle) as in the Pan Docs tables. */
#ifndef SM83_H
#define SM83_H
#include <stdint.h>
typedef struct {
  uint8_t a, f, b, c, d, e, h, l;
  uint16_t sp, pc;
  unsigned long cyc;
  int halted, bad;
  uint8_t (*rd)(uint16_t);
  void (*wr)(uint16_t, uint8_t);
} sm83;
#define FZ 0x80
#define FN 0x40
#define FH 0x20
#define FC 0x10
static uint8_t s_fetch(sm83 *s) { return s->rd(s->pc++); }
static uint16_t s_fetch16(sm83 *s) { uint16_t l = s_fetch(s); return l | (uint16_t)s_fetch(s) << 8; }
static uint16_t s_get16(sm83 *s, int i) { /* 0 BC 1 DE 2 HL 3 SP */
  switch (i) { case 0: return s->b << 8 | s->c; case 1: return s->d << 8 | s->e;
  case 2: return s->h << 8 | s->l; default: return s->sp; } }
static void s_set16(sm83 *s, int i, uint16_t v) {
  switch (i) { case 0: s->b = v >> 8; s->c = v; break; case 1: s->d = v >> 8; s->e = v; break;
  case 2: s->h = v >> 8; s->l = v; break; default: s->sp = v; } }
static uint8_t s_getr(sm83 *s, int r) {
  switch (r) { case 0: return s->b; case 1: return s->c; case 2: return s->d; case 3: return s->e;
  case 4: return s->h; case 5: return s->l; case 6: s->cyc += 4; return s->rd(s->h << 8 | s->l); default: return s->a; } }
static void s_setr(sm83 *s, int r, uint8_t v) {
  switch (r) { case 0: s->b = v; break; case 1: s->c = v; break; case 2: s->d = v; break; case 3: s->e = v; break;
  case 4: s->h = v; break; case 5: s->l = v; break; case 6: s->cyc += 4; s->wr(s->h << 8 | s->l, v); break; default: s->a = v; } }
static void s_push(sm83 *s, uint16_t v) { s->wr(--s->sp, v >> 8); s->wr(--s->sp, v); }
static uint16_t s_pop(sm83 *s) { uint16_t l = s->rd(s->sp++); return l | (uint16_t)s->rd(s->sp++) << 8; }
static int s_cond(sm83 *s, int cc) {
  switch (cc) { case 0: return !(s->f & FZ); case 1: return !!(s->f & FZ); case 2: return !(s->f & FC); default: return !!(s->f & FC); } }
static void s_alu(sm83 *s, int op, uint8_t v) {
  unsigned a = s->a, r, c = (s->f & FC) ? 1 : 0;
  switch (op) {
  case 0: r = a + v; s->f = ((r & 0xFF) ? 0 : FZ) | (((a & 15) + (v & 15)) > 15 ? FH : 0) | (r > 255 ? FC : 0); s->a = r; break;
  case 1: r = a + v + c; s->f = ((r & 0xFF) ? 0 : FZ) | (((a & 15) + (v & 15) + c) > 15 ? FH : 0) | (r > 255 ? FC : 0); s->a = r; break;
  case 2: case 7: r = a - v; s->f = ((r & 0xFF) ? 0 : FZ) | FN | ((a & 15) < (v & 15) ? FH : 0) | (a < v ? FC : 0); if (op == 2) s->a = r; break;
  case 3: r = a - v - c; s->f = ((r & 0xFF) ? 0 : FZ) | FN | ((int)(a & 15) - (int)(v & 15) - (int)c < 0 ? FH : 0) | ((int)a - (int)v - (int)c < 0 ? FC : 0); s->a = r; break;
  case 4: s->a &= v; s->f = (s->a ? 0 : FZ) | FH; break;
  case 5: s->a ^= v; s->f = s->a ? 0 : FZ; break;
  case 6: s->a |= v; s->f = s->a ? 0 : FZ; break;
  }
}
static void s_cb(sm83 *s) {
  int op = s_fetch(s), r = op & 7, y = (op >> 3) & 7; uint8_t v, c;
  s->cyc += 8;
  v = s_getr(s, r);
  switch (op >> 6) {
  case 0:
    c = (s->f & FC) ? 1 : 0;
    switch (y) {
    case 0: c = v >> 7; v = v << 1 | c; break;
    case 1: c = v & 1; v = v >> 1 | c << 7; break;
    case 2: { uint8_t n = v >> 7; v = v << 1 | c; c = n; } break;
    case 3: { uint8_t n = v & 1; v = v >> 1 | c << 7; c = n; } break;
    case 4: c = v >> 7; v <<= 1; break;
    case 5: c = v & 1; v = (v >> 1) | (v & 0x80); break;
    case 6: v = v << 4 | v >> 4; c = 0; break;
    case 7: c = v & 1; v >>= 1; break;
    }
    s->f = (v ? 0 : FZ) | (c ? FC : 0);
    s_setr(s, r, v); break;
  case 1: s->f = (s->f & FC) | FH | ((v >> y) & 1 ? 0 : FZ); break;
  case 2: s_setr(s, r, v & ~(1 << y)); break;
  case 3: s_setr(s, r, v | (1 << y)); break;
  }
}
/* one instruction */
static void sm83_step(sm83 *s) {
  uint8_t op = s_fetch(s), v; uint16_t w; int8_t e;
  if (op >= 0x40 && op < 0x80) {
    if (op == 0x76) { s->halted = 1; s->cyc += 4; return; }
    s->cyc += 4; v = s_getr(s, (op) & 7); s_setr(s, (op >> 3) & 7, v); return; }
  if (op >= 0x80 && op < 0xC0) { s->cyc += 4; s_alu(s, (op >> 3) & 7, s_getr(s, op & 7)); return; }
  switch (op) {
  case 0x00: s->cyc += 4; return;
  case 0x01: case 0x11: case 0x21: case 0x31: s_set16(s, op >> 4, s_fetch16(s)); s->cyc += 12; return;
  case 0x02: s->wr(s_get16(s, 0), s->a); s->cyc += 8; return;
  case 0x12: s->wr(s_get16(s, 1), s->a); s->cyc += 8; return;
  case 0x22: w = s_get16(s, 2); s->wr(w, s->a); s_set16(s, 2, w + 1); s->cyc += 8; return;
  case 0x32: w = s_get16(s, 2); s->wr(w, s->a); s_set16(s, 2, w - 1); s->cyc += 8; return;
  case 0x0A: s->a = s->rd(s_get16(s, 0)); s->cyc += 8; return;
  case 0x1A: s->a = s->rd(s_get16(s, 1)); s->cyc += 8; return;
  case 0x2A: w = s_get16(s, 2); s->a = s->rd(w); s_set16(s, 2, w + 1); s->cyc += 8; return;
  case 0x3A: w = s_get16(s, 2); s->a = s->rd(w); s_set16(s, 2, w - 1); s->cyc += 8; return;
  case 0x03: case 0x13: case 0x23: case 0x33: s_set16(s, op >> 4, s_get16(s, op >> 4) + 1); s->cyc += 8; return;
  case 0x0B: case 0x1B: case 0x2B: case 0x3B: s_set16(s, op >> 4, s_get16(s, op >> 4) - 1); s->cyc += 8; return;
  case 0x09: case 0x19: case 0x29: case 0x39: {
    unsigned hl = s_get16(s, 2), x = s_get16(s, op >> 4), r = hl + x;
    s->f = (s->f & FZ) | (((hl & 0xFFF) + (x & 0xFFF)) > 0xFFF ? FH : 0) | (r > 0xFFFF ? FC : 0);
    s_set16(s, 2, r); s->cyc += 8; return; }
  case 0x04: case 0x0C: case 0x14: case 0x1C: case 0x24: case 0x2C: case 0x34: case 0x3C: {
    int r = op >> 3; s->cyc += 4; v = s_getr(s, r) + 1;
    s->f = (s->f & FC) | (v ? 0 : FZ) | ((v & 15) == 0 ? FH : 0); s_setr(s, r, v); return; }
  case 0x05: case 0x0D: case 0x15: case 0x1D: case 0x25: case 0x2D: case 0x35: case 0x3D: {
    int r = op >> 3; s->cyc += 4; v = s_getr(s, r) - 1;
    s->f = (s->f & FC) | FN | (v ? 0 : FZ) | ((v & 15) == 15 ? FH : 0); s_setr(s, r, v); return; }
  case 0x06: case 0x0E: case 0x16: case 0x1E: case 0x26: case 0x2E: case 0x36: case 0x3E: {
    int r = op >> 3; s->cyc += 8; v = s_fetch(s); s_setr(s, r, v); return; }
  case 0x07: s->a = s->a << 1 | s->a >> 7; s->f = (s->a & 1) ? FC : 0; s->cyc += 4; return;
  case 0x0F: s->f = (s->a & 1) ? FC : 0; s->a = s->a >> 1 | s->a << 7; s->cyc += 4; return;
  case 0x17: { uint8_t c = (s->f & FC) ? 1 : 0; s->f = (s->a & 0x80) ? FC : 0; s->a = s->a << 1 | c; s->cyc += 4; return; }
  case 0x1F: { uint8_t c = (s->f & FC) ? 0x80 : 0; s->f = (s->a & 1) ? FC : 0; s->a = s->a >> 1 | c; s->cyc += 4; return; }
  case 0x08: w = s_fetch16(s); s->wr(w, s->sp); s->wr(w + 1, s->sp >> 8); s->cyc += 20; return;
  case 0x10: s->halted = 1; s->cyc += 4; return;
  case 0x18: e = s_fetch(s); s->pc += e; s->cyc += 12; return;
  case 0x20: case 0x28: case 0x30: case 0x38:
    e = s_fetch(s); if (s_cond(s, (op >> 3) & 3)) { s->pc += e; s->cyc += 12; } else s->cyc += 8; return;
  case 0x27: { unsigned a = s->a; int adj = 0;
    if (s->f & FN) { if (s->f & FH) adj |= 6; if (s->f & FC) adj |= 0x60; a -= adj; }
    else { if ((s->f & FH) || (a & 15) > 9) adj |= 6; if ((s->f & FC) || a > 0x99) adj |= 0x60; a += adj; }
    s->f = (s->f & FN) | ((adj & 0x60) ? FC : 0) | ((a & 0xFF) ? 0 : FZ); s->a = a; s->cyc += 4; return; }
  case 0x2F: s->a = ~s->a; s->f |= FN | FH; s->cyc += 4; return;
  case 0x37: s->f = (s->f & FZ) | FC; s->cyc += 4; return;
  case 0x3F: s->f = (s->f & FZ) | ((s->f & FC) ? 0 : FC); s->cyc += 4; return;
  case 0xC0: case 0xC8: case 0xD0: case 0xD8:
    if (s_cond(s, (op >> 3) & 3)) { s->pc = s_pop(s); s->cyc += 20; } else s->cyc += 8; return;
  case 0xC9: case 0xD9: s->pc = s_pop(s); s->cyc += 16; return;
  case 0xC1: case 0xD1: case 0xE1: w = s_pop(s); s_set16(s, (op >> 4) - 0xC, w); s->cyc += 12; return;
  case 0xF1: w = s_pop(s); s->a = w >> 8; s->f = w & 0xF0; s->cyc += 12; return;
  case 0xC5: case 0xD5: case 0xE5: s_push(s, s_get16(s, (op >> 4) - 0xC)); s->cyc += 16; return;
  case 0xF5: s_push(s, s->a << 8 | s->f); s->cyc += 16; return;
  case 0xC2: case 0xCA: case 0xD2: case 0xDA:
    w = s_fetch16(s); if (s_cond(s, (op >> 3) & 3)) { s->pc = w; s->cyc += 16; } else s->cyc += 12; return;
  case 0xC3: s->pc = s_fetch16(s); s->cyc += 16; return;
  case 0xE9: s->pc = s_get16(s, 2); s->cyc += 4; return;
  case 0xC4: case 0xCC: case 0xD4: case 0xDC:
    w = s_fetch16(s); if (s_cond(s, (op >> 3) & 3)) { s_push(s, s->pc); s->pc = w; s->cyc += 24; } else s->cyc += 12; return;
  case 0xCD: w = s_fetch16(s); s_push(s, s->pc); s->pc = w; s->cyc += 24; return;
  case 0xC7: case 0xCF: case 0xD7: case 0xDF: case 0xE7: case 0xEF: case 0xF7: case 0xFF:
    s_push(s, s->pc); s->pc = op & 0x38; s->cyc += 16; return;
  case 0xC6: case 0xCE: case 0xD6: case 0xDE: case 0xE6: case 0xEE: case 0xF6: case 0xFE:
    s_alu(s, (op >> 3) & 7, s_fetch(s)); s->cyc += 8; return;
  case 0xCB: s_cb(s); return;
  case 0xE0: s->wr(0xFF00 | s_fetch(s), s->a); s->cyc += 12; return;
  case 0xF0: s->a = s->rd(0xFF00 | s_fetch(s)); s->cyc += 12; return;
  case 0xE2: s->wr(0xFF00 | s->c, s->a); s->cyc += 8; return;
  case 0xF2: s->a = s->rd(0xFF00 | s->c); s->cyc += 8; return;
  case 0xEA: s->wr(s_fetch16(s), s->a); s->cyc += 16; return;
  case 0xFA: s->a = s->rd(s_fetch16(s)); s->cyc += 16; return;
  case 0xE8: case 0xF8: { e = s_fetch(s); unsigned sp = s->sp, r = sp + e;
    s->f = (((sp & 15) + ((uint8_t)e & 15)) > 15 ? FH : 0) | (((sp & 255) + (uint8_t)e) > 255 ? FC : 0);
    if (op == 0xE8) { s->sp = r; s->cyc += 16; } else { s_set16(s, 2, r); s->cyc += 12; } return; }
  case 0xF9: s->sp = s_get16(s, 2); s->cyc += 8; return;
  case 0xF3: case 0xFB: s->cyc += 4; return;
  default: s->bad = 1; s->halted = 1; return;
  }
}
#endif
