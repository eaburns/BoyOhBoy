#include "gameboy.h"

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

bool gameboy_eq(const Gameboy *a, const Gameboy *b) {
  return memcmp(a->cpu.registers, b->cpu.registers, sizeof(a->cpu.registers)) ==
             0 &&
         a->cpu.flags == b->cpu.flags && a->cpu.sp == b->cpu.sp &&
         a->cpu.pc == b->cpu.pc && a->cpu.ir == b->cpu.ir &&
         a->cpu.ime == b->cpu.ime &&
         (a->cpu.bank == b->cpu.bank ||
          a->cpu.bank == NULL && b->cpu.bank == instructions ||
          a->cpu.bank == instructions && b->cpu.bank == NULL) &&
         a->cpu.cycle == b->cpu.cycle &&
         memcmp(a->cpu.scratch, b->cpu.scratch, sizeof(a->cpu.scratch)) == 0 &&
         memcmp(a->mem, b->mem, sizeof(a->mem)) == 0;
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
  if (!(a->cpu.bank == b->cpu.bank ||
        a->cpu.bank == NULL && b->cpu.bank == instructions ||
        a->cpu.bank == instructions && b->cpu.bank == NULL)) {
    fprintf(f, "bank: %p != %p\n", a->cpu.bank, b->cpu.bank);
  }
  if (a->cpu.cycle != b->cpu.cycle) {
    fprintf(f, "cycle: %d != %d\n", a->cpu.cycle, b->cpu.cycle);
  }
  for (int i = 0; i < sizeof(a->cpu.scratch); i++) {
    if (a->cpu.scratch[i] != b->cpu.scratch[i]) {
      fprintf(f, "scratch[%d]: %d ($%02x) != %d ($%02x)\n", i,
              a->cpu.scratch[i], a->cpu.scratch[i], b->cpu.scratch[i],
              b->cpu.scratch[i]);
    }
  }
  for (int i = 0; i < sizeof(a->mem); i++) {
    if (a->mem[i] != b->mem[i]) {
      fprintf(f, "mem[$%04x]: %d ($%02x) != %d ($%02x)\n", i, a->mem[i],
              a->mem[i], b->mem[i], b->mem[i]);
    }
  }
}