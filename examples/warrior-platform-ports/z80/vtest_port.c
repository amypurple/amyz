/* vtest.c: run a ColecoVision VRAM decompressor in a Z80 emulator (superzazu/z80)
   with a TMS9918A port model, check the VRAM result and report timing.
   usage: vtest code.bin compressed.bin original.bin [dest_hex [table.bin]]
   code.bin is loaded at $8000 and entered there with HL=$C000 (compressed data),
   DE=dest. RAM is $7000-$73FF (SP starts at $7400). Times are taken at the end of
   each I/O instruction (instruction granularity), which is what the >=29 T rule
   between VRAM accesses is checked against.
   RAM mode (environment AMYRAM=1, for amyz_ram / amyzf_ram): the output goes to
   main RAM at dest (use 4000: $4000-$6FFF holds up to 12 KB); writes inside
   [dest, dest+len) are allowed, the result is compared byte for byte with the
   original in RAM, and any VDP port access counts as an error (vdp_io=). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "z80.h"
static uint8_t mem[65536], vram[16384], ramw[65536];
static int latch_full, latch_lo, vaddr, readbuf;
static unsigned long last_data = 0, last_addr2 = 0, min_gap = 1e9, min_addr_gap = 1e9, ndata = 0, nviol = 0;
static int have_data = 0, have_addr = 0, wrote_ram_outside = 0, ram_mode = 0, out_lo = 0, out_hi = 0;
static unsigned long vdp_io = 0;
static int vdp_data = 0xBE;   /* VDP data port; control = data + 1 (env VDPDATA=98 for MSX) */
static z80 cpu; static unsigned long prof[65536];
static uint8_t rb(void* u, uint16_t a) { (void)u; return mem[a]; }
static void wb(void* u, uint16_t a, uint8_t v) { (void)u; if (ram_mode && a >= out_lo && a < out_hi) { mem[a] = v; return; }
  if (a < 0x7000 || a >= 0x7400) wrote_ram_outside = 1; else ramw[a] = 1; mem[a] = v; }
static void data_access(void) {
  unsigned long t = cpu.cyc;
  if (have_data) { unsigned long g = t - last_data; if (g < min_gap) min_gap = g; if (g < 29) nviol++; }
  if (have_addr) { unsigned long g = t - last_addr2; if (g < min_addr_gap) min_addr_gap = g; have_addr = 0; }
  last_data = t; have_data = 1; ndata++;
}
static uint8_t pin(z80* z, uint8_t p) {
  (void)z; if ((p & 0xFE) == vdp_data) vdp_io++;
  if (p == vdp_data) {
    data_access(); latch_full = 0; uint8_t v = readbuf; readbuf = vram[vaddr]; vaddr = (vaddr + 1) & 0x3FFF; return v; }
  if (p == vdp_data + 1) { latch_full = 0; return 0x80; }
  return 0xFF;
}
static void pout(z80* z, uint8_t p, uint8_t v) {
  (void)z; if ((p & 0xFE) == vdp_data) vdp_io++;
  if (p == vdp_data) { data_access(); latch_full = 0; vram[vaddr] = v; readbuf = v; vaddr = (vaddr + 1) & 0x3FFF; return; }
  if (p == vdp_data + 1) {
    if (!latch_full) { latch_lo = v; latch_full = 1; return; }
    latch_full = 0;
    if ((v & 0xC0) == 0x80) return; /* register write */
    vaddr = (latch_lo | (v << 8)) & 0x3FFF;
    if (!(v & 0x40)) { readbuf = vram[vaddr]; vaddr = (vaddr + 1) & 0x3FFF; }
    last_addr2 = cpu.cyc; have_addr = 1; have_data = 0; /* data-to-data gap restarts after an address set */
  }
}
static long load(const char* f, uint8_t* dst, long max) {
  FILE* fp = fopen(f, "rb"); if (!fp) { perror(f); exit(2); }
  long n = fread(dst, 1, max, fp); fclose(fp); return n;
}
int main(int argc, char** argv) {
  static uint8_t orig[16384];
  if (argc < 4) return 2;
  int dest = argc > 4 ? (int)strtol(argv[4], 0, 16) : 0;
  long nc = load(argv[1], mem + 0x8000, 0x4000);
  long nz = load(argv[2], mem + 0xC000, 0x3FF0);
  long no = load(argv[3], orig, 16384);
  if (argc > 5) load(argv[5], mem + 0xBF00, 0x100);   /* optional table (AMYZ) at $BF00 */
  memset(vram, 0xA5, sizeof vram);
  if (getenv("VDPDATA")) vdp_data = (int)strtol(getenv("VDPDATA"), 0, 16);
  ram_mode = getenv("AMYRAM") && atoi(getenv("AMYRAM"));
  if (ram_mode) { out_lo = dest; out_hi = dest + (int)no; memset(mem + out_lo, 0xA5, no); if (out_hi > 0x7000) { printf("FAIL output too big for RAM mode\n"); return 2; } }
  z80_init(&cpu); cpu.read_byte = rb; cpu.write_byte = wb; cpu.port_in = pin; cpu.port_out = pout;
  cpu.pc = 0x8000; cpu.sp = 0x7400; cpu.h = 0xC0; cpu.l = 0x00; cpu.d = dest >> 8; cpu.e = dest & 0xFF;
  mem[0xFFF0] = 0x76; cpu.sp -= 2; mem[cpu.sp] = 0xF0; mem[cpu.sp + 1] = 0xFF;
  unsigned minsp = cpu.sp; unsigned long steps = 0;
  while (cpu.pc != 0xFFF0 && steps < 200000000UL) { unsigned short pc0 = cpu.pc; unsigned long c0 = cpu.cyc; z80_step(&cpu); prof[pc0] += cpu.cyc - c0; steps++; if (cpu.sp < minsp && cpu.sp >= 0x7000) minsp = cpu.sp; }
  uint8_t* res = ram_mode ? mem + dest : vram + dest;
  int ok = cpu.pc == 0xFFF0 && memcmp(res, orig, no) == 0 && !wrote_ram_outside && !(ram_mode && vdp_io);
  int ramused = 0; for (int a = 0x7000; a < 0x7400; a++) ramused += ramw[a];
  printf("%s code=%ld comp=%ld out=%ld T=%lu T/byte=%.1f data_accesses=%lu min_data_gap=%lu min_addr_to_data=%lu gaps_under_29=%lu stack=%u ram_bytes_written=%d outside_ram_write=%d vdp_io=%lu mode=%s\n",
    ok ? "OK" : "FAIL", nc, nz, no, cpu.cyc, (double)cpu.cyc / no, ndata, min_gap == 1e9 ? 0 : min_gap, min_addr_gap == 1e9 ? 0 : min_addr_gap, nviol, 0x7400 - minsp, ramused, wrote_ram_outside, vdp_io, ram_mode ? "ram" : "vram");
  if (!ok) { for (long i = 0; i < no; i++) if (res[i] != orig[i]) { printf("first diff at %ld: %02X vs %02X\n", i, res[i], orig[i]); break; } }
  if (getenv("VPROF")) { FILE* f = fopen(getenv("VPROF"), "w"); for (int a = 0; a < 65536; a++) if (prof[a]) fprintf(f, "%04X %lu\n", a, prof[a]); fclose(f); }
  return ok ? 0 : 1;
}
