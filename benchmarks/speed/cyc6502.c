/* cyc6502.c - cycles of one AMYZ decode with a 6502 decoder (harness core cpu6502.h).
 * usage: cyc6502 decoder.bin table stream original
 * decoder at $0800 (entry), table at $0700, ZP at $f0, stream at $8300, output at $0a00.
 * Prints: OK|FAIL cycles(incl. jsr) bytes */
#include <stdio.h>
#include <string.h>
#include "../harness/cpu6502.h"
static uint8_t mem[65536];
static long rf(const char *p, uint8_t *b, long m) { FILE *f = fopen(p, "rb"); if (!f) return -1; long n = fread(b, 1, m, f); fclose(f); return n; }
int main(int argc, char **argv) {
    static uint8_t orig[65536];
    if (argc < 5) return 2;
    rf(argv[1], mem + 0x800, 0x200); rf(argv[2], mem + 0x700, 64);
    long sn = rf(argv[3], mem + 0x8300, 0x7c00), on = rf(argv[4], orig, sizeof orig);
    if (sn <= 0 || on <= 0 || on > 0x7900) { printf("FAIL size\n"); return 2; }
    mem[0xf0] = 0x00; mem[0xf1] = 0x83; mem[0xf2] = 0x00; mem[0xf3] = 0x0a;
    cpu6502 c = { 0 }; c.mem = mem; c.s = 0xfd; c.p = 0x24; c.pc = 0x800;
    mem[0x1fe] = 0xff; mem[0x1ff] = 0x02;
    while (c.pc != 0x300 && !c.bad && c.cycles < 400000000) step(&c);
    int ok = !c.bad && c.pc == 0x300 && !memcmp(mem + 0xa00, orig, on);
    printf("%s %llu %ld\n", ok ? "OK" : "FAIL", (unsigned long long)c.cycles + 6, on);
    return !ok;
}
