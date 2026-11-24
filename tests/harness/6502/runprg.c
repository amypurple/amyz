/* runprg.c - run an amyzpack .prg on the 6502 core (flat 64 KB RAM, no ROM,
 * no I/O) from $080d until the PC reaches the start address (or $a659 for
 * BASIC RUN), then compare memory with the original files.
 * usage: runprg packed.prg stop_hex orig.prg|raw.bin@hex ... */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu6502.h"

static uint8_t mem[65536];
int main(int argc, char **argv) {
    if (argc < 4) return 2;
    static uint8_t buf[70000];
    FILE *f = fopen(argv[1], "rb"); long n = fread(buf, 1, sizeof buf, f); fclose(f);
    int la = buf[0] | buf[1] << 8;
    memset(mem, 0x55, sizeof mem); mem[1] = 0x37;
    memcpy(mem + la, buf + 2, n - 2);
    int stop = strtol(argv[2], 0, 16);
    cpu6502 c = { 0 }; c.mem = mem; c.s = 0xf6; c.p = 0x24; c.pc = 0x080d;
    while ((c.pc != stop || mem[1] != 0x37) && !c.bad && c.cycles < 200000000) step(&c);
    printf("stopped at $%04x after %llu cycles%s\n", c.pc, (unsigned long long)c.cycles, c.bad ? " (BAD OPCODE)" : "");
    int fail = c.pc != stop || mem[1] != 0x37;
    for (int i = 3; i < argc; i++) {
        char name[512]; strncpy(name, argv[i], sizeof name - 1); name[sizeof name - 1] = 0;
        char *at = strrchr(name, '@'); int addr = -1;
        if (at) { *at = 0; addr = strtol(at + 1, 0, 16); }
        f = fopen(name, "rb"); n = fread(buf, 1, sizeof buf, f); fclose(f);
        uint8_t *d = buf; if (addr < 0) { addr = buf[0] | buf[1] << 8; d += 2; n -= 2; }
        int bad = memcmp(mem + addr, d, n) != 0;
        if (bad) for (long k = 0; k < n; k++) if (mem[addr + k] != d[k]) { printf("first diff at $%04lx\n", addr + k); break; }
        printf("%s $%04x %ld B: %s\n", name, addr, n, bad ? "MISMATCH" : "identical");
        fail |= bad;
    }
    printf("$2d/$2e = $%02x%02x\n", mem[0x2e], mem[0x2d]);
    return fail;
}
