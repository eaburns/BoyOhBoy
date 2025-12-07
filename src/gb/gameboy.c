#include "gameboy.h"

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void fail(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");
  abort();
}

Rom read_rom(const char *path) {
  FILE *in = fopen(path, "r");
  if (in == NULL) {
    fail("failed to open %s: %s", path, strerror(errno));
  }
  int size = 0;
  uint8_t *data = NULL;
  for (;;) {
    char buf[4096];
    int n = fread(buf, 1, sizeof(buf), in);
    data = realloc(data, size + n);
    memcpy(data + size, buf, n);
    size += n;
    if (n < sizeof(buf)) {
      break;
    }
  }
  if (ferror(in)) {
    fail("failed to read from %s: %s", path, strerror(errno));
  }
  if (fclose(in) != 0) {
    fail("failed to close %s: %s", path, strerror(errno));
  }
  Rom rom = {.data = data, .size = size};
  return rom;
}

void free_rom(Rom *rom) { free((void *)rom->data); }

Gameboy init_gameboy(const Rom *rom) {
  Gameboy g = {};
  g.rom = rom;

  memcpy(g.mem, rom->data, rom->size < MEM_ROM_END ? rom->size : MEM_ROM_END);

  // Starting state of DMG after running the boot ROM and ending at 0x0101.
  g.cpu.registers[REG_B] = 0x00;
  g.cpu.registers[REG_C] = 0x13;
  g.cpu.registers[REG_D] = 0x00;
  g.cpu.registers[REG_E] = 0xD3;
  g.cpu.registers[REG_H] = 0x01;
  g.cpu.registers[REG_L] = 0x4D;
  g.cpu.registers[REG_A] = 0x01;
  g.cpu.ir = 0x0; // NOP
  g.cpu.pc = 0x0101;
  g.cpu.sp = 0xFFFE;
  g.cpu.flags = FLAG_Z;
  g.mem[MEM_P1_JOYPAD] = 0xCF;
  return g;
}

bool gameboy_eq(const Gameboy *a, const Gameboy *b) {
  return memcmp(a->cpu.registers, b->cpu.registers, sizeof(a->cpu.registers)) ==
             0 &&
         a->cpu.flags == b->cpu.flags && a->cpu.sp == b->cpu.sp &&
         a->cpu.pc == b->cpu.pc && a->cpu.ir == b->cpu.ir &&
         a->cpu.ime == b->cpu.ime && a->cpu.ei_pend == b->cpu.ei_pend &&
         a->cpu.state == b->cpu.state &&
         (a->cpu.bank == b->cpu.bank ||
          a->cpu.bank == NULL && b->cpu.bank == instructions ||
          a->cpu.bank == instructions && b->cpu.bank == NULL) &&
         a->cpu.cycle == b->cpu.cycle && a->cpu.w == b->cpu.w &&
         a->cpu.z == b->cpu.z && memcmp(a->mem, b->mem, sizeof(a->mem)) == 0;
}

void gameboy_print_diff(FILE *f, const Gameboy *a, const Gameboy *b) {
  for (int i = 0; i < sizeof(a->cpu.registers); i++) {
    if (a->cpu.registers[i] != b->cpu.registers[i]) {
      fprintf(f, "registers[%s]: %d ($%02x) != %d ($%02x) \n", reg8_name(i),
              a->cpu.registers[i], a->cpu.registers[i], b->cpu.registers[i],
              b->cpu.registers[i]);
    }
  }
  if (a->cpu.flags != b->cpu.flags) {
    fprintf(f, "flags: $%02x != $%02x\n", a->cpu.flags, b->cpu.flags);
  }
  if (a->cpu.sp != b->cpu.sp) {
    fprintf(f, "sp: %d ($%02x) != %d ($%02x)\n", a->cpu.sp, a->cpu.sp,
            b->cpu.sp, b->cpu.sp);
  }
  if (a->cpu.pc != b->cpu.pc) {
    fprintf(f, "pc: %d ($%02x) != %d ($%02x)\n", a->cpu.pc, a->cpu.pc,
            b->cpu.pc, b->cpu.pc);
  }
  if (a->cpu.ir != b->cpu.ir) {
    fprintf(f, "ir: %d ($%02x) != %d ($%02x)\n", a->cpu.ir, a->cpu.ir,
            b->cpu.ir, b->cpu.ir);
  }
  if (a->cpu.ime != b->cpu.ime) {
    fprintf(f, "ime: %d != %d\n", a->cpu.ime, b->cpu.ime);
  }
  if (a->cpu.ei_pend != b->cpu.ei_pend) {
    fprintf(f, "ei_pend: %d != %d\n", a->cpu.ei_pend, b->cpu.ei_pend);
  }
  if (a->cpu.state != b->cpu.state) {
    fprintf(f, "state: %s != %s\n", cpu_state_name(a->cpu.state),
            cpu_state_name(b->cpu.state));
  }
  if (!(a->cpu.bank == b->cpu.bank ||
        a->cpu.bank == NULL && b->cpu.bank == instructions ||
        a->cpu.bank == instructions && b->cpu.bank == NULL)) {
    fprintf(f, "bank: %p != %p\n", a->cpu.bank, b->cpu.bank);
  }
  if (a->cpu.cycle != b->cpu.cycle) {
    fprintf(f, "cycle: %d != %d\n", a->cpu.cycle, b->cpu.cycle);
  }
  if (a->cpu.w != b->cpu.w) {
    fprintf(f, "w: %d ($%02x) != %d ($%02x)\n", a->cpu.w, a->cpu.w, b->cpu.w,
            b->cpu.w);
  }
  if (a->cpu.z != b->cpu.z) {
    fprintf(f, "z: %d ($%02x) != %d ($%02x)\n", a->cpu.z, a->cpu.z, b->cpu.z,
            b->cpu.z);
  }
  for (int i = 0; i < sizeof(a->mem); i++) {
    if (a->mem[i] != b->mem[i]) {
      fprintf(f, "mem[$%04x]: %d ($%02x) != %d ($%02x)\n", i, a->mem[i],
              a->mem[i], b->mem[i], b->mem[i]);
    }
  }
}

void mcycle(Gameboy *g) {
  do {
    cpu_mcycle(g);
    if (g->dma_ticks_remaining > 0) {
      uint16_t offs = DMA_MCYCLES - g->dma_ticks_remaining;
      uint16_t src = g->mem[MEM_DMA] * 0x100 + offs;
      uint16_t dst = MEM_OAM_START + offs;
      g->mem[dst] = g->mem[src];
      g->dma_ticks_remaining--;
    }
    ppu_tcycle(g);
    ppu_tcycle(g);
    ppu_tcycle(g);
    ppu_tcycle(g);
    g->div++;
    if (g->div == 0) {
      g->mem[MEM_DIV] ++;
    }
  } while (g->cpu.state == EXECUTING || g->cpu.state == INTERRUPTING);
}