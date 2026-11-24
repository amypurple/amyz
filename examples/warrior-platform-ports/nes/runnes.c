/* runnes.c - AMYZ NES PPU decoder harness (6502 core with a PPU model).
 * usage: runnes decoder.bin streams_dir tables_dir corpus_dir
 * Decoder at $0800, table at $0700, stream at $8000; output to PPU $0000.
 * PPU model: $2006 two-write address latch (high, low; $2002 read resets it),
 * $2007 write (increment 1), $2007 read through the one-byte read buffer,
 * 16 KB of PPU memory. Compares PPU memory with the original byte for byte,
 * and checks that the decoder writes no CPU RAM outside its 11 zero-page bytes
 * and the stack. Reports cycles per output byte. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu6502io.h"

static uint8_t mem[65536], ppu[16384];
static int latch, paddr, rbuf, badw;
static int ppurd(uint16_t a, uint8_t *v) {
    if (a == 0x2002) { latch = 0; *v = 0x80; return 1; }
    if (a == 0x2007) { *v = rbuf; rbuf = ppu[paddr & 0x3fff]; paddr = (paddr + 1) & 0x3fff; return 1; }
    if (a >= 0x2000 && a < 0x4000) { *v = 0; return 1; }
    return 0;
}
static void ppuwr(uint16_t a, uint8_t v) {
    if (a == 0x2006) { if (!latch) paddr = (paddr & 0xff) | (v & 0x3f) << 8; else paddr = (paddr & 0x3f00) | v; latch ^= 1; return; }
    if (a == 0x2007) { ppu[paddr & 0x3fff] = v; paddr = (paddr + 1) & 0x3fff; return; }
    if (a < 0x100) { if (a >= 0xf0 && a < 0xfb) return; badw++; return; }
    if (a >= 0x100 && a < 0x200) return;
    if (a >= 0x2000 && a < 0x4000) return;
    badw++;
}
static long rf(const char *p, uint8_t *b, long m) { FILE *f = fopen(p, "rb"); if (!f) return -1; long n = fread(b, 1, m, f); fclose(f); return n; }
int main(int argc, char **argv) {
    if (argc < 5) return 2;
    static uint8_t code[4096], tab[64], strm[32768], orig[32768];
    long cn = rf(argv[1], code, sizeof code);
    char p[1024], line[512]; snprintf(p, sizeof p, "%s/files.tsv", argv[2]);
    FILE *tsv = fopen(p, "r"); if (!tsv) return 2;
    int ok = 0, fail = 0; double cyc = 0, by = 0;
    rd_hook = ppurd;
    while (fgets(line, sizeof line, tsv)) {
        char cat[64], name[256], game[128]; int csz;
        if (sscanf(line, "%63s %255s %d %127s", cat, name, &csz, game) != 4) continue;
        snprintf(p, sizeof p, "%s/%s.tabz", argv[3], game); rf(p, tab, 64);
        snprintf(p, sizeof p, "%s/%s/%s", argv[2], cat, name); long sn = rf(p, strm, sizeof strm);
        if (!strcmp(cat, "warrior")) snprintf(p, sizeof p, "%s/../../wgame/warrior/%s", argv[4], name);
        else snprintf(p, sizeof p, "%s/%s/%s", argv[4], cat, name);
        long on = rf(p, orig, sizeof orig);
        memset(mem, 0, sizeof mem); memset(ppu, 0xa5, sizeof ppu);
        memcpy(mem + 0x800, code, cn); memcpy(mem + 0x700, tab, 15); memcpy(mem + 0x8000, strm, sn);
        mem[0xf0] = 0x00; mem[0xf1] = 0x80; mem[0xf2] = 0; mem[0xf3] = 0;
        cpu6502 c = { 0 }; c.mem = mem; c.on_write = ppuwr; c.s = 0xfd; c.p = 0x24; c.pc = 0x800;
        mem[0x1fe] = 0xff; mem[0x1ff] = 0x02; latch = 0; paddr = 0; badw = 0;
        while (c.pc != 0x300 && !c.bad && c.cycles < 400000000) step(&c);
        int good = !c.bad && c.pc == 0x300 && !memcmp(ppu, orig, on) && ppu[on] == 0xa5 && !badw;
        if (good) { ok++; cyc += c.cycles + 6; by += on; } else { fail++; printf("FAIL %s/%s bad=%x badw=%d\n", cat, name, c.bad, badw); }
    }
    printf("ok %d fail %d, %.1f cycles per output byte\n", ok, fail, cyc / by);
    return fail != 0;
}
