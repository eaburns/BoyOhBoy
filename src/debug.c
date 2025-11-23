#include "gb/gameboy.h"

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  LINE_MAX = 128,

  HALT = 0x76,
};

void step(Gameboy *g) {
  do {
    cpu_mcycle(g);
  } while (g->cpu.state == EXECUTING || g->cpu.state == INTERRUPTING);
}

static void print_current_instruction(const Gameboy *g) {
  // IR has already been fetched into PC, so we go back one,
  // except for HALT, which doesn't increment PC.
  Addr pc = g->cpu.ir == HALT ? g->cpu.pc : g->cpu.pc - 1;
  Disasm disasm = disassemble(g->mem, pc);
  printf("%s\n", disasm.full);
}

struct {
  const char *name;
  Reg8 r;
} reg8_names[] = {
    {"A", REG_A}, {"B", REG_B}, {"C", REG_C}, {"D", REG_D},
    {"E", REG_E}, {"H", REG_H}, {"L", REG_L},
};

struct {
  const char *name;
  Reg16 r;
} reg16_names[] = {
    {"BC", REG_BC}, {"DE", REG_DE}, {"HL", REG_HL},
    {"SP", REG_SP}, {"AF", REG_AF},
};

void do_print(Gameboy *g, const char *arg_in) {
  char arg[LINE_MAX] = {};
  for (int i = 0; i < strlen(arg_in); i++) {
    arg[i] = toupper(arg_in[i]);
  }
  for (int i = 0; i < sizeof(reg8_names) / sizeof(reg8_names[0]); i++) {
    if (strcmp(arg, reg8_names[i].name) == 0) {
      uint8_t x = get_reg8(&g->cpu, reg8_names[i].r);
      printf("%s=%d ($%02x)\n", arg, x, x);
      return;
    }
  }
  for (int i = 0; i < sizeof(reg16_names) / sizeof(reg16_names[0]); i++) {
    if (strcmp(arg, reg16_names[i].name) == 0) {
      uint16_t x = get_reg16(&g->cpu, reg16_names[i].r);
      printf("%s=%d ($%04x)\n", arg, x, x);
      return;
    }
  }
  if (strcmp(arg, "FLAGS") == 0) {
    printf("FLAGS=$%02x\n", g->cpu.flags >> 8);
    return;
  }
  if (strcmp(arg, "Z") == 0) {
    printf("Z=%d\n", (g->cpu.flags | FLAG_Z) == 1);
    return;
  }
  if (strcmp(arg, "NZ") == 0) {
    printf("NZ=%d\n", (g->cpu.flags | FLAG_Z) == 0);
    return;
  }
  if (strcmp(arg, "C") == 0) {
    printf("C=%d\n", (g->cpu.flags | FLAG_C) == 1);
    return;
  }
  if (strcmp(arg, "NC") == 0) {
    printf("NC=%d\n", (g->cpu.flags | FLAG_C) == 0);
    return;
  }
  if (strcmp(arg, "H") == 0) {
    printf("H=%d\n", (g->cpu.flags | FLAG_H) == 0);
    return;
  }
  if (strcmp(arg, "N") == 0) {
    printf("N=%d\n", (g->cpu.flags | FLAG_N) == 0);
    return;
  }

  printf("unknown argument %s\n", arg);
  return;
}

int main(int argc, const char *argv[]) {
  if (argc != 2) {
    fail("expected 1 argument, got %d", argc);
  }

  Rom rom = read_rom(argv[1]);
  Gameboy g = init_gameboy(&rom);

  bool go = false;
  for (;;) {
    print_current_instruction(&g);
    step(&g);

    while (!go) {
      char line[LINE_MAX];
      if (fgets(line, sizeof(line), stdin) == NULL) {
        fail("error reading stdin");
      }
      // If the line is just \n, then break the read loop and step.
      if (strlen(line) == 1) {
        break;
      }
      if (line[strlen(line) - 1] != '\n') {
        fail("line too long (max %d characters)", sizeof(line) - 1);
      }
      line[strlen(line) - 1] = '\0';

      char arg[LINE_MAX];
      if (sscanf(line, "print %s", arg) == 1) {
        do_print(&g, arg);
      } else if (strcmp(line, "go") == 0) {
        go = true;
      }
    }
  }
}