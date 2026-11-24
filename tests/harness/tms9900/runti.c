/* runti.c - AMYZ TMS9900 (TI-99/4A) VRAM decoder harness.
 * usage: runti decoder.bin streams_dir tables_dir orig_dir
 * Decoder image at >A000 (BLWP vector first), table at >B000, stream at >B100,
 * caller workspace >8320, decoder workspace >8300 (scratchpad). VRAM output at
 * address 0 is compared with the original byte for byte. VDP model: 2-byte
 * address latch at >8C02 (bit 6 of the second byte = write), data write >8C00,
 * data read >8800 with the TMS9918A read-ahead buffer. Reports cycles with no
 * wait states and with TI-99/4A wait states, and the smallest gap between two
 * VDP data accesses. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tms9900.h"

static uint8_t mem[65536], vram[16384];
static int latch_full, latch_lo, vaddr, readbuf, vbad;
static uint64_t lastdata, mingap[2] = { ~0ull, ~0ull };
static int curws;
static uint64_t *clk;
static void gap(void) {
    if (lastdata) { uint64_t g = *clk - lastdata; if (g < mingap[curws]) mingap[curws] = g; }
    lastdata = *clk;
}
static int ior(tms *t, uint16_t a, uint8_t *v) {
    (void)t;
    if ((a & 0xfffe) == 0x8800) { gap(); *v = readbuf; readbuf = vram[vaddr]; vaddr = (vaddr + 1) & 0x3fff; latch_full = 0; return 1; }
    if (a >= 0x8800 && a < 0x8c00) { vbad++; *v = 0; return 1; }
    if (a >= 0x8c00 && a < 0x9000) { *v = 0; return 1; }   /* read-before-write of a write port: no effect */
    return 0;
}
static int iow(tms *t, uint16_t a, uint8_t v) {
    (void)t;
    if ((a & 0xfffe) == 0x8c00) { gap(); vram[vaddr] = v; readbuf = v; vaddr = (vaddr + 1) & 0x3fff; latch_full = 0; return 1; }
    if ((a & 0xfffe) == 0x8c02) {
        if (!latch_full) { latch_lo = v; latch_full = 1; }
        else {
            latch_full = 0; vaddr = ((v & 0x3f) << 8) | latch_lo;
            if (v & 0x80) { /* register write: not used by the decoder */ vbad++; }
            else if (!(v & 0x40)) { readbuf = vram[vaddr]; vaddr = (vaddr + 1) & 0x3fff; }
        }
        return 1;
    }
    if (a >= 0x8800 && a < 0x9000) { vbad++; return 1; }
    return 0;
}
static long rf(const char *p, uint8_t *b, long m) { FILE *f = fopen(p, "rb"); if (!f) return -1; long n = fread(b, 1, m, f); fclose(f); return n; }

int main(int argc, char **argv) {
    if (argc < 5) return 2;
    static uint8_t code[8192], tab[64], strm[32768], orig[32768];
    long cn = rf(argv[1], code, sizeof code);
    int ram = argc > 5 && !strcmp(argv[5], "ram");   /* RAM decoder: output at >C000 */
    char p[1024], line[512];
    snprintf(p, sizeof p, "%s/files.tsv", argv[2]);
    FILE *tsv = fopen(p, "r"); if (!tsv) return 2;
    const char *cats[] = { "patterns", "colours", "names", "comic", "warrior" };
    double cyc[2][5] = { { 0 } }, by[5] = { 0 };
    int ok = 0, fail = 0;
    while (fgets(line, sizeof line, tsv)) {
        char cat[64], name[256], game[128]; int csz;
        if (sscanf(line, "%63s %255s %d %127s", cat, name, &csz, game) != 4) continue;
        snprintf(p, sizeof p, "%s/%s.tabz", argv[3], game);
        if (rf(p, tab, 64) != 15) { printf("TABLE? %s\n", p); return 2; }
        snprintf(p, sizeof p, "%s/%s/%s", argv[2], cat, name);
        long sn = rf(p, strm, sizeof strm);
        if (!strcmp(cat, "warrior")) snprintf(p, sizeof p, "%s/../../wgame/warrior/%s", argv[4], name);
        else snprintf(p, sizeof p, "%s/%s/%s", argv[4], cat, name);
        long on = rf(p, orig, sizeof orig);
        if (sn != csz || on <= 0) { printf("MISSING %s\n", name); fail++; continue; }
        int good = 1; uint64_t c2[2] = { 0, 0 };
        for (int ws = 0; ws < 2; ws++) {
            memset(mem, 0, sizeof mem); memset(vram, 0xe5, sizeof vram);
            memcpy(mem + 0xa000, code, cn); memcpy(mem + 0xb000, tab, 15); memcpy(mem + 0xb100, strm, sn);
            /* caller: BLWP @>A000 ; JMP $ at >A800 */
            mem[0xa800] = 0x04; mem[0xa801] = 0x20; mem[0xa802] = 0xa0; mem[0xa803] = 0x00; mem[0xa804] = 0x10; mem[0xa805] = 0xff;
            mem[0x8320] = 0xb1; mem[0x8321] = 0x00;          /* caller R0 = stream */
            mem[0x8322] = ram ? 0xc0 : 0x00; mem[0x8323] = 0x00; /* caller R1 = VRAM 0 or >C000 */
            tms t = { 0 }; t.mem = mem; t.wp = 0x8320; t.pc = 0xa800; t.waitstates = ws; t.io_read = ior; t.io_write = iow;
            clk = &t.cycles; lastdata = 0; curws = ws; latch_full = 0; vbad = 0;
            while (t.pc != 0xa804 && !t.bad && t.cycles < 400000000) tms_step(&t);
            int endv = mem[0x8322] << 8 | mem[0x8323];
            int g = !t.bad && t.pc == 0xa804 && !vbad && (ram ? (!memcmp(mem + 0xc000, orig, on) && endv == 0xc000 + on && mem[0xc000 + on] == 0) : (!memcmp(vram, orig, on) && endv == on && vram[on] == 0xe5));
            for (long k = 0; k < 0xa000; k++) if (k < 0x8300 || k >= 0x8324) { if (mem[k]) { g = 0; printf("stray write at >%04lx\n", k); break; } }
            if (!g) { good = 0; printf("FAIL %s/%s ws=%d bad=%x pc=%04x end=%04x vbad=%d\n", cat, name, ws, t.bad, t.pc, endv, vbad); break; }
            c2[ws] = t.cycles;
        }
        if (good) ok++; else { fail++; continue; }
        int ci = 0; for (int i = 0; i < 5; i++) if (!strcmp(cat, cats[i])) ci = i;
        cyc[0][ci] += c2[0]; cyc[1][ci] += c2[1]; by[ci] += on;
    }
    printf("ok %d fail %d\n", ok, fail);
    double a = 0, b = 0, n = 0;
    printf("%-9s %8s %14s %14s\n", "category", "bytes", "cyc/B (0 ws)", "cyc/B (TI ws)");
    for (int i = 0; i < 5; i++) { printf("%-9s %8.0f %14.1f %14.1f\n", cats[i], by[i], cyc[0][i] / by[i], cyc[1][i] / by[i]); a += cyc[0][i]; b += cyc[1][i]; n += by[i]; }
    printf("%-9s %8.0f %14.1f %14.1f\n", "all", n, a / n, b / n);
    for (int w = 0; w < 2; w++) printf("smallest gap between VDP data accesses (%s): %llu cycles = %.2f us at 3 MHz\n", w ? "TI wait states" : "no wait states", (unsigned long long)mingap[w], mingap[w] / 3.0);
    return fail != 0;
}
