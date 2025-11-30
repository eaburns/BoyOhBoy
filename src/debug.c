#include "gb/gameboy.h"

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static sig_atomic_t go = false;
static sig_atomic_t done = false;

enum { LINE_MAX = 128 };

void sigint_handler(int s) {
  if (go) {
    printf("\n");
    go = false;
  } else {
    done = true;
  }
}

static void print_current_instruction(const Gameboy *g) {
  static const uint16_t HALT = 0x76;
  // IR has already been fetched into PC, so we go back one,
  // except for HALT, which doesn't increment PC.
  Addr pc = g->cpu.ir == HALT ? g->cpu.pc : g->cpu.pc - 1;
  Disasm disasm = disassemble(g->mem, pc);
  printf("%s\n", disasm.full);
}

static const struct {
  const char *name;
  enum { REG8, REG16 } size;
  union {
    Reg8 r8;
    Reg16 r16;
  };
} regs[] = {
    {.name = "B", .size = REG8, .r8 = REG_B},
    {.name = "C", .size = REG8, .r8 = REG_C},
    {.name = "BC", .size = REG16, .r16 = REG_BC},
    {.name = "D", .size = REG8, .r8 = REG_D},
    {.name = "E", .size = REG8, .r8 = REG_E},
    {.name = "DE", .size = REG16, .r16 = REG_DE},
    {.name = "H", .size = REG8, .r8 = REG_H},
    {.name = "L", .size = REG8, .r8 = REG_L},
    {.name = "HL", .size = REG16, .r16 = REG_HL},
    {.name = "A", .size = REG8, .r8 = REG_A},
    {.name = "F", .size = REG8, .r8 = REG_F},
    {.name = "AF", .size = REG16, .r16 = REG_AF},
    {.name = "SP", .size = REG16, .r16 = REG_SP},
    {.name = "PC", .size = REG16, .r16 = REG_PC},
    {.name = "IR", .size = REG16, .r16 = REG_IR},
};

static void do_reg(Gameboy *g, const char *arg_in) {
  char arg[LINE_MAX] = {};
  for (int i = 0; i < strlen(arg_in); i++) {
    arg[i] = toupper(arg_in[i]);
  }
  for (int i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
    if (strcmp(arg, regs[i].name) != 0) {
      continue;
    }
    if (regs[i].size == REG8) {
      uint8_t x = get_reg8(&g->cpu, regs[i].r8);
      printf("%s:%d ($%02X)\n", arg, x, x);
    } else {
      uint16_t x = get_reg16(&g->cpu, regs[i].r16);
      printf("%s: %d ($%04X)\n", arg, x, x);
    }
    return;
  }
  printf("Unknown register %s\nRegisters are: ", arg);
  for (int i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
    if (i > 0) {
      printf(", ");
    }
    printf("%s", regs[i].name);
  }
  printf("\n");
}

static void do_dump(const Gameboy *g) {
  static const int NCOL = 3;
  int col = 0;
  for (int i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
    if (regs[i].size == REG8) {
      uint8_t x = get_reg8(&g->cpu, regs[i].r8);
      printf("%s:  %-5d ($%02X)  ", regs[i].name, x, x);
    } else {
      uint16_t x = get_reg16(&g->cpu, regs[i].r16);
      printf("%2s: %-5d ($%04X)", regs[i].name, x, x);
    }
    printf(col == NCOL - 1 ? "\n" : "\t");
    col = (col + 1) % NCOL;
  }
}

// Various named memory locations.
static const struct {
  const char *name;
  uint16_t addr;
} mems[] = {
    {"DIV", MEM_DIV},   {"TIMA", MEM_TIMA}, {"TMA", MEM_TMA},
    {"TAC", MEM_TAC},   {"IF", MEM_IF},     {"LCDC", MEM_LCDC},
    {"STAT", MEM_STAT}, {"SCX", MEM_SCX},   {"SCY", MEM_SCY},
    {"LY", MEM_LY},     {"DMA", MEM_DMA},   {"BGP", MEM_BGP},
    {"OBP0", MEM_OBP0}, {"OBP1", MEM_OBP1}, {"IE", MEM_IE},
};

static void do_peek(const Gameboy *g, const char *arg_in) {
  char arg[LINE_MAX] = {};
  for (int i = 0; i < strlen(arg_in); i++) {
    arg[i] = toupper(arg_in[i]);
  }
  for (int i = 0; i < sizeof(mems) / sizeof(mems[0]); i++) {
    if (strcmp(arg, mems[i].name) == 0) {
      uint8_t x = g->mem[mems[i].addr];
      printf("%s ($%04X): %d ($%02X)\n", mems[i].name, mems[i].addr, x, x);
      return;
    }
  }

  int addr = 0;
  if (sscanf(arg_in, "%d", &addr) != 1 && sscanf(arg_in, "$%x", &addr) != 1) {
    printf("Invalid peek: %s\n", arg_in);
    printf("Expected a named location, decimal, or $hex address\n");
    printf("Available named locations are: ");
    for (int i = 0; i < sizeof(mems) / sizeof(mems[0]); i++) {
      if (i > 0) {
        printf(", ");
      }
      printf("%s", mems[i].name);
    }
    printf("\n");
    return;
  }
  if (addr < 0 || addr > 0xFFFF) {
    printf("Invalid address %d ($%04X), must be in range 0-0xFFFF\n", addr,
           addr);
    return;
  }
  uint8_t x = g->mem[addr];
  for (int i = 0; i < sizeof(mems) / sizeof(mems[0]); i++) {
    if (mems[i].addr == addr) {
      printf("%s ($%04X): %d ($%02X)\n", mems[i].name, mems[i].addr, x, x);
      return;
    }
  }
  printf("$%04X: %d ($%02X)\n", addr, x, x);
}

typedef struct {
  char *data;
  int size, cap;
} Buffer;

static void bprintf(Buffer *b, const char *fmt, ...) {
  char one[1];
  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(one, 1, fmt, args);
  va_end(args);

  if (b->size + n + 1 >= b->cap) {
    if (b->cap == 0) {
      b->cap = 32;
    }
    while (b->size + n + 1 >= b->cap) {
      b->cap *= 2;
    }
    b->data = realloc(b->data, b->cap);
  }

  va_start(args, fmt);
  vsnprintf(b->data + b->size, n + 1, fmt, args);
  va_end(args);
  b->size += n;
}

static const char *px_str(int px) {
  switch (px) {
  case 0:
    return " ";
  case 1:
    return ".";
  case 2:
    return "x";
  case 3:
    return "0";
  default:
    fail("impossible pixel value %d\n", px);
  }
  return ""; // impossible
}

enum { MAX_TILE_INDEX = 384 };

static void do_tile(const Gameboy *g, int tile_index) {
  if (tile_index < 0 || tile_index > MAX_TILE_INDEX) {
    printf("tile index must be between 0 and %d\n", MAX_TILE_INDEX);
    return;
  }
  Buffer b = {};
  bprintf(&b, "tile %d:\n", tile_index);
  uint16_t addr = MEM_TILE_BLOCK0_START + tile_index * 16;
  bprintf(&b, "$%04X-$%04X: ", addr, addr + 16 - 1);
  for (int i = 0; i < 16; i++) {
    bprintf(&b, "%02X ", g->mem[addr + i]);
  }
  bprintf(&b, "\n");
  bprintf(&b, "+--------+\n");
  for (int y = 0; y < 8; y++) {
    bprintf(&b, "|");
    uint8_t row_low = g->mem[addr++];
    uint8_t row_high = g->mem[addr++];
    for (int x = 0; x < 8; x++) {
      uint8_t px_low = row_low >> (7 - x) & 1;
      uint8_t px_high = row_high >> (7 - x) & 1;
      bprintf(&b, "%s", px_str(px_high << 1 | px_low));
    }
    bprintf(&b, "|\n");
  }
  bprintf(&b, "+--------+\n");
  printf("%s", b.data);
  free(b.data);
}

static void do_tilemap(const Gameboy *g) {
  int row = 0;
  int row_start = 0;
  static const int COLS = 24;
  Buffer b = {};
  for (int i = 0; i < 9 * COLS; i++) {
    bprintf(&b, "-");
  }
  bprintf(&b, "\n");
  while (row_start <= MAX_TILE_INDEX) {
    for (int y = 0; y < 8; y++) {
      int tile = row_start;
      bprintf(&b, "|");
      for (int col = 0; col < COLS; col++) {
        if (tile > MAX_TILE_INDEX) {
          bprintf(&b, "        |");
          continue;
        }
        uint16_t addr = MEM_TILE_BLOCK0_START + tile * 16;
        uint8_t row_low = g->mem[addr + y * 2];
        uint8_t row_high = g->mem[addr + y * 2 + 1];
        for (int x = 0; x < 8; x++) {
          uint8_t px_low = row_low >> (7 - x) & 1;
          uint8_t px_high = row_high >> (7 - x) & 1;
          bprintf(&b, "%s", px_str(px_high << 1 | px_low));
        }
        bprintf(&b, "|");
        tile++;
      }
      bprintf(&b, "\n");
    }
    for (int i = 0; i < 9 * COLS; i++) {
      bprintf(&b, "-");
    }
    bprintf(&b, "\n");
    row_start += COLS;
  }
  printf("%s", b.data);
  free(b.data);
}

static void do_bgmap(const Gameboy *g, int map_index) {
  if (map_index != 0 && map_index != 1) {
    printf("bgmap must be 0 or 1\n");
    return;
  }
  Buffer b = {};
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
          bprintf(&b, "%s", px_str(px_high << 1 | px_low));
        }
      }
      bprintf(&b, "\n");
    }
  }
  printf("%s", b.data);
  free(b.data);
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
  if (sscanf(line, "reg %s", arg_s) == 1) {
    do_reg(g, arg_s);
  } else if (sscanf(line, "peek %s", arg_s) == 1) {
    do_peek(g, arg_s);
  } else if (sscanf(line, "tile %d", &arg_d) == 1) {
    do_tile(g, arg_d);
  } else if (strcmp(line, "tilemap") == 0) {
    do_tilemap(g);
  } else if (sscanf(line, "bgmap %d", &arg_d) == 1) {
    do_bgmap(g, arg_d);
  } else if (strcmp(line, "dump") == 0) {
    do_dump(g);
  } else if (strcmp(line, "go") == 0) {
    go = true;
  } else if (strcmp(line, "quit") == 0) {
    done = true;
  }
  return true;
}

static double time_ns() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1000000000 + (double)ts.tv_nsec;
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
