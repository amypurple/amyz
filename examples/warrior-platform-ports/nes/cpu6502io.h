/* cpu6502io.h - cpu6502.h with a read hook (rd_hook) for memory-mapped I/O.
 * Based on cpu6502.h - minimal NMOS 6502 core (documented opcodes only), written for the
 * AMYZ port harness. Cycle counts include page-crossing and branch penalties.
 * Decimal mode is not implemented (ADC/SBC binary only; the decoders never set D).
 * Public domain / CC0, by Claude for Amy Studio. */
#ifndef CPU6502IO_H
#define CPU6502IO_H
#ifndef CPU6502_H
#define CPU6502_H
#include <stdint.h>

typedef struct {
    uint8_t a, x, y, s, p;  /* p: N V - B D I Z C */
    uint16_t pc;
    uint64_t cycles;
    uint8_t *mem;           /* 64 KB */
    int bad;                /* set on an unimplemented opcode */
    void (*on_write)(uint16_t addr, uint8_t v);
} cpu6502;

#define FC 0x01
#define FZ 0x02
#define FI 0x04
#define FD 0x08
#define FV 0x40
#define FN 0x80

static int (*rd_hook)(uint16_t a, uint8_t *v);   /* optional memory-mapped reads */
static inline uint8_t rd(cpu6502 *c, uint16_t a) { uint8_t v; if (rd_hook && rd_hook(a, &v)) return v; return c->mem[a]; }
static inline void wr(cpu6502 *c, uint16_t a, uint8_t v) { if (c->on_write) c->on_write(a, v); c->mem[a] = v; }
static inline void nz(cpu6502 *c, uint8_t v) { c->p = (c->p & ~(FN|FZ)) | (v & FN) | (v ? 0 : FZ); }
static inline void push(cpu6502 *c, uint8_t v) { wr(c, 0x100 + c->s, v); c->s--; }
static inline uint8_t pop(cpu6502 *c) { c->s++; return rd(c, 0x100 + c->s); }

static void adc(cpu6502 *c, uint8_t v) {
    unsigned r = c->a + v + (c->p & FC);
    c->p &= ~(FC|FV);
    if (r > 0xff) c->p |= FC;
    if (~(c->a ^ v) & (c->a ^ r) & 0x80) c->p |= FV;
    c->a = (uint8_t)r; nz(c, c->a);
}
static void cmpv(cpu6502 *c, uint8_t r, uint8_t v) {
    c->p = (c->p & ~FC) | (r >= v ? FC : 0); nz(c, (uint8_t)(r - v));
}

/* run one instruction */
static void step(cpu6502 *c) {
    uint8_t op = rd(c, c->pc++);
    uint16_t ea = 0; int cyc = 0, mode, pen = 0;
    /* addressing mode table index: 0 imp,1 imm,2 zp,3 zpx,4 zpy,5 abs,6 abx,7 aby,8 izx,9 izy,10 ind,11 rel,12 acc */
    static const uint8_t M[256] = {
     0,8,0,0,0,2,2,0,0,1,12,0,0,5,5,0, 11,9,0,0,0,3,3,0,0,7,0,0,0,6,6,0,
     5,8,0,0,2,2,2,0,0,1,12,0,5,5,5,0, 11,9,0,0,0,3,3,0,0,7,0,0,0,6,6,0,
     0,8,0,0,0,2,2,0,0,1,12,0,5,5,5,0, 11,9,0,0,0,3,3,0,0,7,0,0,0,6,6,0,
     0,8,0,0,0,2,2,0,0,1,12,0,10,5,5,0, 11,9,0,0,0,3,3,0,0,7,0,0,0,6,6,0,
     0,8,0,0,2,2,2,0,0,0,0,0,5,5,5,0, 11,9,0,0,3,3,4,0,0,7,0,0,0,6,0,0,
     1,8,1,0,2,2,2,0,0,1,0,0,5,5,5,0, 11,9,0,0,3,3,4,0,0,7,0,0,6,6,7,0,
     1,8,0,0,2,2,2,0,0,1,0,0,5,5,5,0, 11,9,0,0,0,3,3,0,0,7,0,0,0,6,6,0,
     1,8,0,0,2,2,2,0,0,1,0,0,5,5,5,0, 11,9,0,0,0,3,3,0,0,7,0,0,0,6,6,0 };
    static const uint8_t T[256] = {
     7,6,0,0,0,3,5,0,3,2,2,0,0,4,6,0, 2,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0,
     6,6,0,0,3,3,5,0,4,2,2,0,4,4,6,0, 2,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0,
     6,6,0,0,0,3,5,0,3,2,2,0,3,4,6,0, 2,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0,
     6,6,0,0,0,3,5,0,4,2,2,0,5,4,6,0, 2,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0,
     0,6,0,0,3,3,3,0,2,0,2,0,4,4,4,0, 2,6,0,0,4,4,4,0,2,5,2,0,0,5,0,0,
     2,6,2,0,3,3,3,0,2,2,2,0,4,4,4,0, 2,5,0,0,4,4,4,0,2,4,2,0,4,4,4,0,
     2,6,0,0,3,3,5,0,2,2,2,0,4,4,6,0, 2,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0,
     2,6,0,0,3,3,5,0,2,2,2,0,4,4,6,0, 2,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0 };
    mode = M[op]; cyc = T[op];
    if (!cyc) { c->bad = op | 0x100; return; }
    switch (mode) {
    case 1: ea = c->pc++; break;
    case 2: ea = rd(c, c->pc++); break;
    case 3: ea = (uint8_t)(rd(c, c->pc++) + c->x); break;
    case 4: ea = (uint8_t)(rd(c, c->pc++) + c->y); break;
    case 5: ea = rd(c, c->pc) | rd(c, c->pc + 1) << 8; c->pc += 2; break;
    case 6: case 7: { uint16_t b = rd(c, c->pc) | rd(c, c->pc + 1) << 8; c->pc += 2;
        ea = b + (mode == 6 ? c->x : c->y); pen = (b ^ ea) & 0xff00; break; }
    case 8: { uint8_t z = rd(c, c->pc++) + c->x; ea = rd(c, z) | rd(c, (uint8_t)(z + 1)) << 8; break; }
    case 9: { uint8_t z = rd(c, c->pc++); uint16_t b = rd(c, z) | rd(c, (uint8_t)(z + 1)) << 8;
        ea = b + c->y; pen = (b ^ ea) & 0xff00; break; }
    case 10: { uint16_t b = rd(c, c->pc) | rd(c, c->pc + 1) << 8; c->pc += 2;
        ea = rd(c, b) | rd(c, (b & 0xff00) | (uint8_t)(b + 1)) << 8; break; }
    case 11: ea = c->pc + 1 + (int8_t)rd(c, c->pc); c->pc++; break;
    }
    /* read instructions with indexed modes pay the page penalty; stores/RMW do not */
#define PEN if (pen) cyc++
#define BR(cond) if (cond) { cyc++; if ((c->pc ^ ea) & 0xff00) cyc++; c->pc = ea; }
    uint8_t v; unsigned t;
    switch (op) {
    /* loads / stores */
    case 0xA9: case 0xA5: case 0xB5: case 0xAD: case 0xBD: case 0xB9: case 0xA1: case 0xB1:
        c->a = rd(c, ea); nz(c, c->a); PEN; break;
    case 0xA2: case 0xA6: case 0xB6: case 0xAE: case 0xBE: c->x = rd(c, ea); nz(c, c->x); PEN; break;
    case 0xA0: case 0xA4: case 0xB4: case 0xAC: case 0xBC: c->y = rd(c, ea); nz(c, c->y); PEN; break;
    case 0x85: case 0x95: case 0x8D: case 0x9D: case 0x99: case 0x81: case 0x91: wr(c, ea, c->a); break;
    case 0x86: case 0x96: case 0x8E: wr(c, ea, c->x); break;
    case 0x84: case 0x94: case 0x8C: wr(c, ea, c->y); break;
    /* ALU */
    case 0x69: case 0x65: case 0x75: case 0x6D: case 0x7D: case 0x79: case 0x61: case 0x71: adc(c, rd(c, ea)); PEN; break;
    case 0xE9: case 0xE5: case 0xF5: case 0xED: case 0xFD: case 0xF9: case 0xE1: case 0xF1: adc(c, ~rd(c, ea)); PEN; break;
    case 0x29: case 0x25: case 0x35: case 0x2D: case 0x3D: case 0x39: case 0x21: case 0x31: c->a &= rd(c, ea); nz(c, c->a); PEN; break;
    case 0x09: case 0x05: case 0x15: case 0x0D: case 0x1D: case 0x19: case 0x01: case 0x11: c->a |= rd(c, ea); nz(c, c->a); PEN; break;
    case 0x49: case 0x45: case 0x55: case 0x4D: case 0x5D: case 0x59: case 0x41: case 0x51: c->a ^= rd(c, ea); nz(c, c->a); PEN; break;
    case 0xC9: case 0xC5: case 0xD5: case 0xCD: case 0xDD: case 0xD9: case 0xC1: case 0xD1: cmpv(c, c->a, rd(c, ea)); PEN; break;
    case 0xE0: case 0xE4: case 0xEC: cmpv(c, c->x, rd(c, ea)); break;
    case 0xC0: case 0xC4: case 0xCC: cmpv(c, c->y, rd(c, ea)); break;
    case 0x24: case 0x2C: v = rd(c, ea); c->p = (c->p & ~(FN|FV|FZ)) | (v & (FN|FV)) | ((v & c->a) ? 0 : FZ); break;
    /* shifts */
    case 0x0A: c->p = (c->p & ~FC) | (c->a >> 7); c->a <<= 1; nz(c, c->a); break;
    case 0x4A: c->p = (c->p & ~FC) | (c->a & 1); c->a >>= 1; nz(c, c->a); break;
    case 0x2A: t = (c->a << 1) | (c->p & FC); c->p = (c->p & ~FC) | (c->a >> 7); c->a = t; nz(c, c->a); break;
    case 0x6A: t = (c->a >> 1) | ((c->p & FC) << 7); c->p = (c->p & ~FC) | (c->a & 1); c->a = t; nz(c, c->a); break;
    case 0x06: case 0x16: case 0x0E: case 0x1E: v = rd(c, ea); c->p = (c->p & ~FC) | (v >> 7); v <<= 1; wr(c, ea, v); nz(c, v); break;
    case 0x46: case 0x56: case 0x4E: case 0x5E: v = rd(c, ea); c->p = (c->p & ~FC) | (v & 1); v >>= 1; wr(c, ea, v); nz(c, v); break;
    case 0x26: case 0x36: case 0x2E: case 0x3E: v = rd(c, ea); t = (v << 1) | (c->p & FC); c->p = (c->p & ~FC) | (v >> 7); v = t; wr(c, ea, v); nz(c, v); break;
    case 0x66: case 0x76: case 0x6E: case 0x7E: v = rd(c, ea); t = (v >> 1) | ((c->p & FC) << 7); c->p = (c->p & ~FC) | (v & 1); v = t; wr(c, ea, v); nz(c, v); break;
    case 0xE6: case 0xF6: case 0xEE: case 0xFE: v = rd(c, ea) + 1; wr(c, ea, v); nz(c, v); break;
    case 0xC6: case 0xD6: case 0xCE: case 0xDE: v = rd(c, ea) - 1; wr(c, ea, v); nz(c, v); break;
    case 0xE8: nz(c, ++c->x); break;
    case 0xCA: nz(c, --c->x); break;
    case 0xC8: nz(c, ++c->y); break;
    case 0x88: nz(c, --c->y); break;
    /* transfers */
    case 0xAA: nz(c, c->x = c->a); break;
    case 0x8A: nz(c, c->a = c->x); break;
    case 0xA8: nz(c, c->y = c->a); break;
    case 0x98: nz(c, c->a = c->y); break;
    case 0xBA: nz(c, c->x = c->s); break;
    case 0x9A: c->s = c->x; break;
    /* stack */
    case 0x48: push(c, c->a); break;
    case 0x68: nz(c, c->a = pop(c)); break;
    case 0x08: push(c, c->p | 0x30); break;
    case 0x28: c->p = pop(c) & ~0x30; break;
    /* flags */
    case 0x18: c->p &= ~FC; break;
    case 0x38: c->p |= FC; break;
    case 0x58: c->p &= ~FI; break;
    case 0x78: c->p |= FI; break;
    case 0xB8: c->p &= ~FV; break;
    case 0xD8: c->p &= ~FD; break;
    case 0xF8: c->p |= FD; break;
    /* branches */
    case 0x10: BR(!(c->p & FN)); break;
    case 0x30: BR(c->p & FN); break;
    case 0x50: BR(!(c->p & FV)); break;
    case 0x70: BR(c->p & FV); break;
    case 0x90: BR(!(c->p & FC)); break;
    case 0xB0: BR(c->p & FC); break;
    case 0xD0: BR(!(c->p & FZ)); break;
    case 0xF0: BR(c->p & FZ); break;
    /* jumps */
    case 0x4C: case 0x6C: c->pc = ea; break;
    case 0x20: t = c->pc - 1; push(c, t >> 8); push(c, t & 0xff); c->pc = ea; break;
    case 0x60: t = pop(c); t |= pop(c) << 8; c->pc = t + 1; break;
    case 0x40: c->p = pop(c) & ~0x30; t = pop(c); t |= pop(c) << 8; c->pc = t; break;
    case 0x00: c->pc++; push(c, c->pc >> 8); push(c, c->pc & 0xff); push(c, c->p | 0x30); c->p |= FI;
        c->pc = rd(c, 0xfffe) | rd(c, 0xffff) << 8; break;
    case 0xEA: break;
    default: c->bad = op | 0x100; return;
    }
    c->cycles += cyc;
}
#endif
#endif
