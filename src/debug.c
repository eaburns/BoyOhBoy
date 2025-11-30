#include "gb/gameboy.h"

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static sig_atomic_t go = false;
static sig_atomic_t done = false;

enum {
  LINE_MAX = 128,

  HALT = 0x76,
};

void sigint_handler(int s) {
  if (go) {
    printf("\n");
    go = false;
  } else {
    done = true;
  }
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

static void do_print(Gameboy *g, const char *arg_in) {
  char arg[LINE_MAX] = {};
  for (int i = 0; i < strlen(arg_in); i++) {
    arg[i] = toupper(arg_in[i]);
  }
  for (int i = 0; i < sizeof(reg8_names) / sizeof(reg8_names[0]); i++) {
    if (strcmp(arg, reg8_names[i].name) == 0) {
      uint8_t x = get_reg8(&g->cpu, reg8_names[i].r);
      printf("%s=%d ($%02X)\n", arg, x, x);
      return;
    }
  }
  for (int i = 0; i < sizeof(reg16_names) / sizeof(reg16_names[0]); i++) {
    if (strcmp(arg, reg16_names[i].name) == 0) {
      uint16_t x = get_reg16(&g->cpu, reg16_names[i].r);
      printf("%s=%d ($%04X)\n", arg, x, x);
      return;
    }
  }
  if (strcmp(arg, "IR") == 0) {
    printf("IR=$%02X\n", g->cpu.ir);
    return;
  }
  if (strcmp(arg, "PC") == 0) {
    printf("PC=$%04X\n", g->cpu.pc);
    return;
  }
  if (strcmp(arg, "FLAGS") == 0) {
    printf("FLAGS=$%02X\n", g->cpu.flags >> 8);
    return;
  }
  if (strcmp(arg, "FLAGS") == 0) {
    printf("FLAGS=$%02X\n", g->cpu.flags >> 8);
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
  if (strcmp(arg, "LCDC") == 0) {
    printf("LCDC=$%02X\n", g->mem[MEM_LCDC]);
    return;
  }
  printf("unknown argument %s\n", arg);
  return;
}

static void print_px(int px) {
  switch (px) {
  case 0:
    printf(" ");
    break;
  case 1:
    printf(".");
    break;
  case 2:
    printf("x");
    break;
  case 3:
    printf("0");
    break;
  default:
    fail("impossible pixel value %d\n", px);
  }
}

enum { MAX_TILE_INDEX = 384 };

static void do_print_tile(const Gameboy *g, int tile_index) {
  if (tile_index < 0 || tile_index > MAX_TILE_INDEX) {
    printf("tile index must be between 0 and %d\n", MAX_TILE_INDEX);
    return;
  }
  printf("tile %d:\n", tile_index);
  uint16_t addr = MEM_TILE_BLOCK0_START + tile_index * 16;
  printf("$%04X-$%04X\n", addr, addr + 16 - 1);
  for (int i = 0; i < 16; i++) {
    printf("%02X ", g->mem[addr + i]);
  }
  printf("\n");

  for (int y = 0; y < 8; y++) {
    uint8_t row_low = g->mem[addr++];
    uint8_t row_high = g->mem[addr++];
    for (int x = 0; x < 8; x++) {
      uint8_t px_low = row_low >> (7 - x) & 1;
      uint8_t px_high = row_high >> (7 - x) & 1;
      print_px(px_high << 1 | px_low);
    }
    printf("\n");
  }
}

static void do_print_tile_map(const Gameboy *g) {
  int row = 0;
  int row_start = 0;
  static const int COLS = 24;
  while (row_start <= MAX_TILE_INDEX) {
    for (int y = 0; y < 8; y++) {
      int tile = row_start;
      for (int col = 0; col < COLS; col++) {
        if (tile > MAX_TILE_INDEX) {
          break;
        }
        uint16_t addr = MEM_TILE_BLOCK0_START + tile * 16;
        uint8_t row_low = g->mem[addr + y * 2];
        uint8_t row_high = g->mem[addr + y * 2 + 1];
        for (int x = 0; x < 8; x++) {
          uint8_t px_low = row_low >> (7 - x) & 1;
          uint8_t px_high = row_high >> (7 - x) & 1;
          print_px(px_high << 1 | px_low);
        }
        printf("|");
        tile++;
      }
      printf("\n");
    }
    for (int i = 0; i < 9 * COLS; i++) {
      printf("-");
    }
    printf("\n");
    row_start += COLS;
  }
}

// /mnt/font/GoMono-Bold/3a/font
static void do_print_bg_map(const Gameboy *g, int map_index) {
  if (map_index != 0 && map_index != 1) {
    printf("bgmap must be 0 or 1\n");
    return;
  }
  uint16_t map_addr =
      map_index == 0 ? MEM_TILE_MAP0_START : MEM_TILE_MAP1_START;
  for (int map_y = 0; map_y < 32; map_y++) {
    for (int y = 0; y < 8; y++) {
      for (int map_x = 0; map_x < 32; map_x++) {
        int tile = g->mem[map_addr + (32 * map_y) + map_x];
        uint16_t addr = MEM_TILE_BLOCK0_START + tile * 16;
        uint8_t row_low = g->mem[addr + y * 2];
        uint8_t row_high = g->mem[addr + y * 2 + 1];
        for (int x = 0; x < 8; x++) {
          uint8_t px_low = row_low >> (7 - x) & 1;
          uint8_t px_high = row_high >> (7 - x) & 1;
          print_px(px_high << 1 | px_low);
        }
      }
      printf("\n");
    }
  }
}

static double time_ns() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1000000000 + (double)ts.tv_nsec;
}

// Returns whether to step the next instruction.
static bool handle_input(Gameboy *g) {
  char line[LINE_MAX];
  printf("> ");
  if (fgets(line, sizeof(line), stdin) == NULL) {
    fail("error reading stdin");
  }
  // If the line is just \n, then break the read loop and step.
  if (strlen(line) == 1) {
    return false;
  }
  if (line[strlen(line) - 1] != '\n') {
    fail("line too long (max %d characters)", sizeof(line) - 1);
  }
  line[strlen(line) - 1] = '\0';

  char arg_s[LINE_MAX];
  int arg_d = 0;
  if (sscanf(line, "print %s", arg_s) == 1) {
    do_print(g, arg_s);
  } else if (sscanf(line, "tile %d", &arg_d) == 1) {
    do_print_tile(g, arg_d);
  } else if (strcmp(line, "tilemap") == 0) {
    do_print_tile_map(g);
  } else if (sscanf(line, "bgmap %d", &arg_d) == 1) {
    do_print_bg_map(g, arg_d);
  } else if (strcmp(line, "go") == 0) {
    go = true;
  } else if (strcmp(line, "quit") == 0) {
    done = true;
  }
  return true;
}

int main(int argc, const char *argv[]) {
  if (argc != 2) {
    fail("expected 1 argument, got %d", argc);
  }

  signal(SIGINT, sigint_handler);

  Rom rom = read_rom(argv[1]);
  Gameboy g = init_gameboy(&rom);

  long num_mcycle = 0;
  double mcycle_ns_avg = 0;

  while (!done) {
    if (!go && g.cpu.state == DONE) {
      print_current_instruction(&g);
      while (!go && !done && handle_input(&g)) {
      }
    }

    PpuMode orig_ppu_mode = g.ppu.mode;
    int orig_ly = g.mem[MEM_LY];
    double start_ns = time_ns();
    mcycle(&g);
    long ns = time_ns() - start_ns;

    if (go) {
      if (num_mcycle == 0) {
        mcycle_ns_avg = ns;
      } else {
        mcycle_ns_avg = mcycle_ns_avg + (ns - mcycle_ns_avg) / (num_mcycle + 1);
      }
      num_mcycle++;
      if (g.break_point) {
        go = false;
        g.break_point = false;
      }
      continue;
    }

    if (num_mcycle > 0) {
      printf("num mcycles: %ld\navg time: %lf ns\n", num_mcycle, mcycle_ns_avg);
      num_mcycle = 0;
    }
    if (g.ppu.mode != orig_ppu_mode) {
      printf("PPU ENTERED %s MODE (LY=%d)\n", ppu_mode_name(g.ppu.mode),
             g.mem[MEM_LY]);
    }
    if (orig_ly < SCREEN_HEIGHT && g.mem[MEM_LY] == SCREEN_HEIGHT) {
      printf("PPU ENTERED VERTICAL BLANK\n");
    }
    g.break_point = false;
  }
  return 0;
}
