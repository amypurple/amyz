/* tms9900.h - minimal TMS9900 core for the AMYZ port harness (by Claude for
 * Amy Studio, public domain / CC0).
 * Scope: the instructions of formats I (A AB C CB S SB SOC SOCB SZC SZCB MOV
 * MOVB), II jumps (JMP JLT JLE JEQ JHE JGT JNE JNC JOC JNO JL JH JOP), V
 * shifts (SRA SRL SLA SRC), VI single operand (B BL BLWP CLR SETO INV NEG ABS
 * SWPB INC INCT DEC DECT; X and ABS are NOT implemented), VIII immediates (LI AI ANDI
 * ORI CI LWPI LIMI) and RTWP. No interrupts, no CRU, no XOP/MPY/DIV/COC/CZC/
 * XOR/STST/STWP (they stop the run with an error).
 * Timing: datasheet clock cycles (base + addressing-mode cost), plus an
 * optional 4-cycle wait state per memory access to "slow" memory (TI-99/4A
 * 8-bit bus: everything except console ROM >0000->1FFF and scratchpad
 * >8000->83FF). Memory accesses are counted as the TMS9900 does them, including
 * the read of a destination before it is written. */
#ifndef TMS9900_H
#define TMS9900_H
#include <stdint.h>

#define ST_LGT 0x8000
#define ST_AGT 0x4000
#define ST_EQ  0x2000
#define ST_C   0x1000
#define ST_OV  0x0800
#define ST_OP  0x0400

typedef struct tms {
    uint16_t pc, wp, st;
    uint64_t cycles;
    uint8_t *mem;                   /* 64 KB, big-endian words */
    int waitstates;                 /* 1: add 4 cycles per slow access */
    int bad;
    /* memory-mapped I/O hooks (byte access to the VDP ports) */
    int (*io_read)(struct tms *t, uint16_t a, uint8_t *v);   /* returns 1 if handled */
    int (*io_write)(struct tms *t, uint16_t a, uint8_t v);
} tms;

static inline void acc(tms *t, uint16_t a) {
    if (t->waitstates && !(a < 0x2000 || (a >= 0x8000 && a < 0x8400))) t->cycles += 4;
}
static inline uint16_t rw(tms *t, uint16_t a) { a &= 0xfffe; acc(t, a); return t->mem[a] << 8 | t->mem[a + 1]; }
static inline void ww(tms *t, uint16_t a, uint16_t v) { a &= 0xfffe; acc(t, a); t->mem[a] = v >> 8; t->mem[a + 1] = v & 0xff; }
static inline uint8_t rb(tms *t, uint16_t a) {
    uint8_t v; acc(t, a & 0xfffe);
    if (t->io_read && t->io_read(t, a, &v)) return v;
    return t->mem[a];
}
static inline void wb(tms *t, uint16_t a, uint8_t v) {
    acc(t, a & 0xfffe);
    if (t->io_write && t->io_write(t, a, v)) return;
    t->mem[a] = v;
}
#define REG(n) ((uint16_t)(t->wp + 2 * (n)))

/* resolve a general address: returns the effective address, adds cycles */
static uint16_t ea(tms *t, int T, int r, int byte) {
    uint16_t a;
    switch (T) {
    case 0: return REG(r);
    case 1: t->cycles += 4; return rw(t, REG(r));
    case 2: t->cycles += 8; { uint16_t x = rw(t, t->pc); t->pc += 2; if (r) x += rw(t, REG(r)); return x; }
    default: t->cycles += byte ? 6 : 8; a = rw(t, REG(r)); ww(t, REG(r), a + (byte ? 1 : 2)); return a;
    }
}
static void cmp0w(tms *t, uint16_t v) {
    t->st &= ~(ST_LGT | ST_AGT | ST_EQ);
    if (v) t->st |= ST_LGT;
    if ((int16_t)v > 0) t->st |= ST_AGT;
    if (!v) t->st |= ST_EQ;
}
static void cmp0b(tms *t, uint8_t v) {
    int p = 0; for (int i = 0; i < 8; i++) p ^= (v >> i) & 1;
    t->st &= ~(ST_LGT | ST_AGT | ST_EQ | ST_OP);
    if (v) t->st |= ST_LGT;
    if ((int8_t)v > 0) t->st |= ST_AGT;
    if (!v) t->st |= ST_EQ;
    if (p) t->st |= ST_OP;
}
static uint16_t addw(tms *t, uint16_t a, uint16_t b) {   /* a + b */
    uint32_t r = (uint32_t)a + b; uint16_t v = r;
    cmp0w(t, v); t->st &= ~(ST_C | ST_OV);
    if (r > 0xffff) t->st |= ST_C;
    if (~(a ^ b) & (a ^ v) & 0x8000) t->st |= ST_OV;
    return v;
}
static uint16_t subw(tms *t, uint16_t d, uint16_t s) {   /* d - s, C = no borrow */
    uint16_t v = d - s;
    cmp0w(t, v); t->st &= ~(ST_C | ST_OV);
    if (d >= s) t->st |= ST_C;
    if ((d ^ s) & (d ^ v) & 0x8000) t->st |= ST_OV;
    return v;
}
static void cmpw(tms *t, uint16_t s, uint16_t d) {
    t->st &= ~(ST_LGT | ST_AGT | ST_EQ);
    if (s > d) t->st |= ST_LGT;
    if ((int16_t)s > (int16_t)d) t->st |= ST_AGT;
    if (s == d) t->st |= ST_EQ;
}

static void tms_step(tms *t) {
    uint16_t op = rw(t, t->pc); t->pc += 2;
    if (op >= 0x4000) {                          /* format I */
        int o = op >> 13, B = (op >> 12) & 1;
        int Td = (op >> 10) & 3, D = (op >> 6) & 15, Ts = (op >> 4) & 3, S = op & 15;
        t->cycles += 14;
        uint16_t sa = ea(t, Ts, S, B), da = ea(t, Td, D, B);
        if (B) {
            uint8_t s = rb(t, sa), d = (o == 6 ? 0 : rb(t, da)), v = 0;   /* MOVB: TMS9900 still reads dest */
            if (o == 6) (void)rb(t, da);
            switch (o) {
            case 2: v = d & ~s; cmp0b(t, v); break;                       /* SZCB */
            case 3: { uint16_t r = subw(t, d << 8, s << 8); v = r >> 8; cmp0b(t, v); break; } /* SB */
            case 4: { uint8_t x = s, y = d; t->st &= ~(ST_LGT | ST_AGT | ST_EQ | ST_OP);
                      if (x > y) t->st |= ST_LGT; if ((int8_t)x > (int8_t)y) t->st |= ST_AGT; if (x == y) t->st |= ST_EQ;
                      { int p = 0; for (int i = 0; i < 8; i++) p ^= (x >> i) & 1; if (p) t->st |= ST_OP; } return; } /* CB */
            case 5: { uint16_t r = addw(t, d << 8, s << 8); v = r >> 8; cmp0b(t, v); break; } /* AB */
            case 6: v = s; cmp0b(t, v); break;                            /* MOVB */
            case 7: v = d | s; cmp0b(t, v); break;                        /* SOCB */
            default: t->bad = op; return;
            }
            wb(t, da, v);
        } else {
            uint16_t s = rw(t, sa), d = rw(t, da), v = 0;
            switch (o) {
            case 2: v = d & ~s; cmp0w(t, v); break;
            case 3: v = subw(t, d, s); break;
            case 4: cmpw(t, s, d); return;
            case 5: v = addw(t, d, s); break;
            case 6: v = s; cmp0w(t, v); break;
            case 7: v = d | s; cmp0w(t, v); break;
            default: t->bad = op; return;
            }
            ww(t, da, v);
        }
        return;
    }
    if (op >= 0x1000 && op < 0x2000) {           /* format II jumps */
        int c = (op >> 8) & 15, j = 0; uint16_t st = t->st;
        switch (c) {
        case 0: j = 1; break;                                     /* JMP */
        case 1: j = !(st & ST_AGT) && !(st & ST_EQ); break;      /* JLT */
        case 2: j = !(st & ST_LGT) || (st & ST_EQ); break;       /* JLE */
        case 3: j = (st & ST_EQ) != 0; break;                    /* JEQ */
        case 4: j = (st & ST_LGT) || (st & ST_EQ); break;        /* JHE */
        case 5: j = (st & ST_AGT) != 0; break;                   /* JGT */
        case 6: j = !(st & ST_EQ); break;                        /* JNE */
        case 7: j = !(st & ST_C); break;                         /* JNC */
        case 8: j = (st & ST_C) != 0; break;                     /* JOC */
        case 9: j = !(st & ST_OV); break;                        /* JNO */
        case 10: j = !(st & ST_LGT) && !(st & ST_EQ); break;     /* JL */
        case 11: j = (st & ST_LGT) && !(st & ST_EQ); break;      /* JH */
        case 12: j = (st & ST_OP) != 0; break;                   /* JOP */
        default: t->bad = op; return;
        }
        if (j) { t->pc += 2 * (int8_t)(op & 0xff); t->cycles += 10; } else t->cycles += 8;
        return;
    }
    if (op >= 0x0800 && op < 0x0c00) {           /* format V shifts */
        int c = (op >> 4) & 15, w = op & 15, kind = (op >> 8) & 3;
        t->cycles += 12;
        if (!c) { c = rw(t, REG(0)) & 15; if (!c) c = 16; t->cycles += 8; }
        t->cycles += 2 * c;
        uint16_t v = rw(t, REG(w)), last = 0, ov = 0;
        for (int i = 0; i < c; i++) {
            uint16_t old = v;
            switch (kind) {
            case 0: last = v & 1; v = (v >> 1) | (v & 0x8000); break;     /* SRA */
            case 1: last = v & 1; v >>= 1; break;                          /* SRL */
            case 2: last = v >> 15; v <<= 1; if ((old ^ v) & 0x8000) ov = 1; break; /* SLA */
            case 3: last = v & 1; v = (v >> 1) | (last << 15); break;     /* SRC */
            }
        }
        ww(t, REG(w), v);
        cmp0w(t, v); t->st &= ~(ST_C | ST_OV);
        if (last) t->st |= ST_C;
        if (ov) t->st |= ST_OV;
        return;
    }
    if (op >= 0x0400 && op < 0x0800) {           /* format VI single operand */
        int k = (op >> 6) & 15, Ts = (op >> 4) & 3, S = op & 15;
        uint16_t a = ea(t, Ts, S, 0), v;
        switch (k) {
        case 0: { t->cycles += 26; uint16_t nw = rw(t, a), np = rw(t, a + 2), ow = t->wp;  /* BLWP */
                  t->wp = nw; ww(t, REG(13), ow); ww(t, REG(14), t->pc); ww(t, REG(15), t->st); t->pc = np; return; }
        case 1: t->cycles += 8; (void)rw(t, a); t->pc = a; return;                          /* B */
        case 3: t->cycles += 10; (void)rw(t, a); ww(t, a, 0); return;                       /* CLR */
        case 4: t->cycles += 12; v = rw(t, a); v = subw(t, 0, v); ww(t, a, v); return;      /* NEG (C/OV approx.) */
        case 5: t->cycles += 10; v = ~rw(t, a); cmp0w(t, v); ww(t, a, v); return;           /* INV */
        case 6: t->cycles += 10; v = addw(t, rw(t, a), 1); ww(t, a, v); return;            /* INC */
        case 7: t->cycles += 10; v = addw(t, rw(t, a), 2); ww(t, a, v); return;            /* INCT */
        case 8: t->cycles += 10; v = subw(t, rw(t, a), 1); ww(t, a, v); return;            /* DEC */
        case 9: t->cycles += 10; v = subw(t, rw(t, a), 2); ww(t, a, v); return;            /* DECT */
        case 10: t->cycles += 12; (void)rw(t, a); ww(t, REG(11), t->pc); t->pc = a; return; /* BL */
        case 11: t->cycles += 10; v = rw(t, a); ww(t, a, (v << 8) | (v >> 8)); return;      /* SWPB */
        case 12: t->cycles += 10; (void)rw(t, a); ww(t, a, 0xffff); return;                 /* SETO */
        default: t->bad = op; return;
        }
    }
    if (op >= 0x0200 && op < 0x0300 && !(op & 0x10)) {   /* format VIII immediates */
        int k = (op >> 5) & 7, w = op & 15;
        uint16_t im = 0;
        if (k != 5 && k != 6) { im = rw(t, t->pc); t->pc += 2; }  /* not STWP/STST */
        switch (k) {
        case 0: t->cycles += 12; (void)rw(t, REG(w)); ww(t, REG(w), im); cmp0w(t, im); return;   /* LI */
        case 1: t->cycles += 14; ww(t, REG(w), addw(t, rw(t, REG(w)), im)); return;               /* AI */
        case 2: t->cycles += 14; { uint16_t v = rw(t, REG(w)) & im; cmp0w(t, v); ww(t, REG(w), v); } return; /* ANDI */
        case 3: t->cycles += 14; { uint16_t v = rw(t, REG(w)) | im; cmp0w(t, v); ww(t, REG(w), v); } return; /* ORI */
        case 4: t->cycles += 14; cmpw(t, rw(t, REG(w)), im); return;                            /* CI */
        case 7: t->cycles += 10; t->wp = im; return;                                            /* LWPI */
        default: t->bad = op; return;
        }
    }
    if (op >= 0x0300 && op < 0x0320) { t->cycles += 16; t->st = (t->st & ~15) | (rw(t, t->pc) & 15); t->pc += 2; return; } /* LIMI */
    if (op == 0x0380) { t->cycles += 14; uint16_t w = t->wp;                                    /* RTWP */
        t->st = rw(t, w + 30); t->pc = rw(t, w + 28); t->wp = rw(t, w + 26); return; }
    t->bad = op | 0x10000;
}
#endif
