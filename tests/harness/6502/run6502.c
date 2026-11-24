/* run6502.c - AMYZ 6502 decoder harness.
 * usage: run6502 decoder.bin entry_hex zpbase_hex small|fast streams_dir tables_dir orig_dir
 * Loads the decoder at $0800, the table at $0700, the stream at $2000, decodes
 * to $4000 and compares with the original byte for byte. Tracks every write:
 * must be inside the output, the declared zero-page bytes, or the stack. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu6502.h"

static uint8_t mem[65536];
static int smc;
static double prof[65536];
static int wlo, whi, zp0, badw, stackmin = 0x200, zpused[256];
static void onw(uint16_t a, uint8_t v) {
    (void)v;
    if (a >= wlo && a < whi) return;
    if (smc && a >= 0x800 && a < 0x1000) return;
    if (a < 0x100) { zpused[a] = 1; if (a >= zp0 && a < zp0 + 11) return; }
    if (a >= 0x100 && a < 0x200) { if (a < stackmin) stackmin = a; return; }
    badw++;
}
static long rf(const char *p, uint8_t *buf, long max) {
    FILE *f = fopen(p, "rb"); if (!f) return -1;
    long n = fread(buf, 1, max, f); fclose(f); return n;
}
int main(int argc, char **argv) {
    if (argc < 8) { fprintf(stderr, "args\n"); return 2; }
    static uint8_t code[4096], tab[64], strm[32768], orig[32768];
    long cn = rf(argv[1], code, sizeof code);
    int entry = strtol(argv[2], 0, 16); zp0 = strtol(argv[3], 0, 16);
    int fast = strcmp(argv[4], "small") != 0; smc = !strcmp(argv[4], "smc");
    char line[512], p[1024];
    snprintf(p, sizeof p, "%s/files.tsv", argv[5]);
    FILE *tsv = fopen(p, "r"); if (!tsv) return 2;
    const char *cats[] = { "patterns", "colours", "names", "comic", "warrior" };
    double ccyc[5] = { 0 }, cby[5] = { 0 };
    int ok = 0, fail = 0;
    uint64_t maxc = 0;
    while (fgets(line, sizeof line, tsv)) {
        char cat[64], name[256], game[128]; int csz;
        if (sscanf(line, "%63s %255s %d %127s", cat, name, &csz, game) != 4) continue;
        memset(mem, 0, sizeof mem);
        memcpy(mem + 0x800, code, cn);
        snprintf(p, sizeof p, "%s/%s.%s", argv[6], game, fast ? "tabzx" : "tabz");
        if (rf(p, tab, 64) != (fast ? 45 : 15)) { printf("TABLE? %s\n", p); return 2; }
        memcpy(mem + 0x700, tab, fast ? 45 : 15);
        snprintf(p, sizeof p, "%s/%s/%s", argv[5], cat, name);
        long sn = rf(p, strm, sizeof strm);
        if (!strcmp(cat, "warrior")) snprintf(p, sizeof p, "%s/../../wgame/warrior/%s", argv[7], name);
        else snprintf(p, sizeof p, "%s/%s/%s", argv[7], cat, name);
        long on = rf(p, orig, sizeof orig);
        if (sn != csz || on <= 0) { printf("MISSING %s sn=%ld on=%ld\n", name, sn, on); fail++; continue; }
        memcpy(mem + 0x2000, strm, sn);
        mem[zp0] = 0x00; mem[zp0 + 1] = 0x20; mem[zp0 + 2] = 0x00; mem[zp0 + 3] = 0x40;
        cpu6502 c = { 0 }; c.mem = mem; c.on_write = onw; c.s = 0xfd; c.p = 0x24;
        mem[0x1fe] = 0xff; mem[0x1ff] = 0x02;   /* return to $0300 */
        c.pc = entry; wlo = 0x4000; whi = 0x4000 + on; badw = 0; stackmin = 0x200;
        while (c.pc != 0x300 && !c.bad && c.cycles < 100000000) { uint16_t pc0 = c.pc; uint64_t c0 = c.cycles; step(&c); prof[pc0] += c.cycles - c0; }
        uint64_t cyc = c.cycles + 6;           /* + the caller's JSR */
        int endp = mem[zp0 + 2] | mem[zp0 + 3] << 8;
        int good = !c.bad && c.pc == 0x300 && !memcmp(mem + 0x4000, orig, on) && !badw && endp == 0x4000 + on;
        if (good) ok++; else { fail++; printf("FAIL %s/%s bad=%x badw=%d end=%04x\n", cat, name, c.bad, badw, endp); }
        int ci = 0; for (int i = 0; i < 5; i++) if (!strcmp(cat, cats[i])) ci = i;
        ccyc[ci] += cyc; cby[ci] += on;
        if (cyc > maxc) maxc = cyc;
        static int stk = 0x200; if (stackmin < stk) { stk = stackmin; printf("stack use so far: %d bytes incl. return address\n", 0x200 - stk); }
    }
    double tc = 0, tb = 0;
    printf("ok %d fail %d\n", ok, fail);
    for (int i = 0; i < 5; i++) { printf("%-9s %8.0f B  %7.1f cyc/B\n", cats[i], cby[i], ccyc[i] / cby[i]); tc += ccyc[i]; tb += cby[i]; }
    printf("all       %8.0f B  %7.1f cyc/B  total %.0f cycles, max file %llu\n", tb, tc / tb, tc, (unsigned long long)maxc);
    int nzp = 0; for (int i = 0; i < 256; i++) nzp += zpused[i];
    printf("zero-page bytes written (all files): %d\n", nzp);
    if (getenv("PROF")) { FILE *pf = fopen(getenv("PROF"), "w"); for (int i = 0; i < 65536; i++) if (prof[i] > 0) fprintf(pf, "%04x %.0f\n", i, prof[i]); fclose(pf); }
    return fail != 0;
}
