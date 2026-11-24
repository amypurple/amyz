/* gbtest.c - run an SM83 AMYZ decoder on one stream and check the output.
   usage: gbtest code.bin stream.bin original.bin table.bin [tabaddr_hex]
   code.bin is a flat image from $0000 (rgblink -x); entry $0200 with
   HL=$4000 (stream), DE=$C000 (output, WRAM); table loaded at tabaddr
   (default $3F00); HRAM $FF80-$FFFE, SP=$FFFE. Any write outside the output
   range, HRAM and the stack is reported. Licence: CC0. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sm83.h"
static uint8_t mem[65536]; static int stray, olo = 0xC000, ohi;
static uint8_t rd(uint16_t a) { return mem[a]; }
static void wr(uint16_t a, uint8_t v) { if (!((a >= olo && a < ohi) || a >= 0xFF80)) stray++; mem[a] = v; }
static long load(const char *f, uint8_t *p, long max) { FILE *fp = fopen(f, "rb"); if (!fp) { perror(f); exit(2); }
  long n = fread(p, 1, max, fp); fclose(fp); return n; }
int main(int argc, char **argv) {
  static uint8_t orig[65536]; sm83 s; long n, sn; int tab = argc > 5 ? strtol(argv[5], 0, 16) : 0x3F00;
  if (argc < 5) { fprintf(stderr, "usage\n"); return 2; }
  load(argv[1], mem, 0x4000);
  memset(mem + 0xC000, 0xEE, 0x2000);
  sn = load(argv[2], mem + 0x4000, 0x8000); n = load(argv[3], orig, 0x2000);
  load(argv[4], mem + tab, 64);
  ohi = olo + n; memset(&s, 0, sizeof s); s.rd = rd; s.wr = wr;
  mem[0x0150] = 0x76; s.sp = 0xFFFE; s_push(&s, 0x0150); s.pc = 0x0200;
  s.h = 0x40; s.l = 0; s.d = 0xC0; s.e = 0;
  long steps = 0; while (!s.halted && steps++ < 100000000L) sm83_step(&s);
  int de = s.d << 8 | s.e;
  int ok = !s.bad && s.pc == 0x0151 && de == ohi && !memcmp(mem + 0xC000, orig, n) && mem[ohi] == 0xEE && !stray;
  printf("%s in=%ld out=%ld T=%lu T/byte=%.1f stray=%d bad=%d pc=%04X de=%04X\n", ok ? "OK" : "FAIL", sn, n, s.cyc, (double)s.cyc / n, stray, s.bad, s.pc, de);
  return !ok;
}
