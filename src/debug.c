#include "9/acme.h"
#include "9/errstr.h"
#include "9/thread.h"
#include "buf/buffer.h"
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

#define DEBUG 0

static const char *CODE_FONT = "/mnt/font/GoMono/11a/font";
static const char *TILE_FONT = "/mnt/font/GoMono/11a/font";
static const char *VRAM_MAP_FONT = "/mnt/font/GoMono-Bold/3a/font";

enum {
  FRAME_HZ = 30,
  VBLANK_HZ = 60,
};
static const double NS_PER_S = 1e9;
static const double FRAME_NS = NS_PER_S / FRAME_HZ;
static const double VBLANK_NS = NS_PER_S / VBLANK_HZ;
static const uint16_t HALT = 0x76;

static Mutex9 mtx;
static Gameboy g;
static Acme *acme = NULL;

// The LCD at the last transition to VBLANK.
// Guarded by mtx;
uint8_t lcd[SCREEN_HEIGHT][SCREEN_WIDTH];
static AcmeWin *lcd_win = NULL;

typedef struct {
  uint16_t addr;
  Disasm disasm;
} DisasmLine;
DisasmLine *lines;
int nlines;
Mem disasm_mem;
static AcmeWin *disasm_win = NULL;

static int step = 0;
static int next_sp = -1;
enum { MAX_BREAKS = 10 };
static int nbreaks = 0;
static int breaks[MAX_BREAKS];

static sig_atomic_t go = false;

// Guarded by mtx;
enum { BUTTON_TIME = 100000 };
static int button_count;

// Maximum size of an input line.
enum { LINE_MAX = 128 };

void sigint_handler(int s) {
  if (go) {
    printf("\n");
    go = false;
  } else {
    exit(0);
  }
}

static int find_disasm_line(uint16_t addr) {
  // TODO: binary search.
  for (int i = 0; i < nlines; i++) {
    if (lines[i].addr > addr) {
      return i - 1;
    }
  }
  return -1;
}

static void update_disasm_lines() {
  int s;
  for (s = 0; s < MEM_SIZE; s++) {
    if (disasm_mem[s] != g.mem[s]) {
      break;
    }
  }
  if (s == MEM_SIZE) {
    return;
  }
  int sline = find_disasm_line(s);
  if (sline >= 0) {
    s = lines[sline].addr;
  }

  int e;
  for (e = MEM_SIZE - 1; e >= 0; e--) {
    if (disasm_mem[e] != g.mem[e]) {
      break;
    }
  }
  e++; // make e exclusive.
  memcpy(disasm_mem + s, g.mem + s, e - s);
  int eline = find_disasm_line(e);

  // We cannot have more than one line per byte of memory,
  // so MEM_SIZE is an upper bound.
  DisasmLine *new_lines = calloc(MEM_SIZE, sizeof(*new_lines));
  if (sline > 0) {
    memcpy(new_lines, lines, sizeof(DisasmLine) * sline);
  }
  int addr = s;
  int i = sline < 0 ? 0 : sline;
  int diff0_start_line = i;
  int diff0_end_line = nlines;
  int diff1_start_line = i;
  int diff1_end_line = i;
  while (addr < MEM_SIZE) {
    new_lines[i].addr = addr;
    new_lines[i].disasm = disassemble(disasm_mem, MEM_SIZE, addr);
    addr += new_lines[i].disasm.size;
    diff1_end_line = i;
    i++;
    if (eline < 0 || addr < e) {
      continue;
    }
    // Once we are beyond the last changed address,
    // look for a line in our original lines that has a matching address.
    // If we find it, the remaining lines will disassemble the same,
    // and we can just copy them over.
    while (eline < nlines && lines[eline].addr < addr) {
      eline++;
    }
    if (eline < nlines && addr == lines[eline].addr) {
      diff1_end_line = i - 1;
      diff0_end_line = eline - 1;
      while (eline < nlines) {
        new_lines[i++] = lines[eline++];
      }
      break;
    }
  }

  if (DEBUG) {
    fprintf(stderr, "addr change $%04X to $%04X (exclusive)\n", s, e);
    fprintf(stderr, "changed lines: %d,%d --> %d,%d\n", diff0_start_line,
            diff0_end_line, diff1_start_line, diff1_end_line);
    if (diff1_end_line - diff1_start_line < 5) {
      for (int j = diff0_start_line; j <= diff0_end_line; j++) {
        fprintf(stderr, "	%s\n", lines[j].disasm.full);
      }
      fprintf(stderr, "changed to\n");
      for (int j = diff1_start_line; j <= diff1_end_line; j++) {
        fprintf(stderr, "	%s\n", new_lines[j].disasm.full);
      }
    }
  }

  free(lines);
  lines = new_lines;
  nlines = i;

  Buffer b = {};
  for (int i = diff1_start_line; i <= diff1_end_line; i++) {
    bprintf(&b, "%s\n", lines[i].disasm.full);
  }
  win_fmt_addr(disasm_win, "%d,%d", diff0_start_line + 1, diff0_end_line + 1);
  win_write_data(disasm_win, b.size, b.data);
  free(b.data);
}

static int split_disasm_line(int line, int addr) {
  if (DEBUG) {
    fprintf(stderr, "splitting a line\n");
  }

  // We cannot have more than one line per byte of memory,
  // so MEM_SIZE is an upper bound.
  DisasmLine *new_lines = calloc(MEM_SIZE, sizeof(*new_lines));
  if (line > 1) {
    memcpy(new_lines, lines, sizeof(DisasmLine) * line - 1);
  }

  int i = line;
  int diff0_start_line = i;
  int diff0_end_line = nlines;
  int diff1_start_line = i;
  int diff1_end_line = i;
  int eline = line + 1;
  int e = line < nlines - 1 ? lines[line + 1].addr : MEM_SIZE;

  uint16_t a = lines[line].addr;
  uint16_t s = a;
  while (a < addr) {
    new_lines[i].addr = a;
    // Limit to 1 byte to make UNKNOWN instructions up to addr.
    new_lines[i].disasm = disassemble(disasm_mem, 1, a);
    a += new_lines[i].disasm.size;
    i++;
    diff1_end_line = i;
  }

  int new_line = i;

  while (addr < MEM_SIZE) {
    new_lines[i].addr = addr;
    new_lines[i].disasm = disassemble(disasm_mem, MEM_SIZE, addr);
    addr += new_lines[i].disasm.size;
    diff1_end_line = i;
    i++;
    if (eline < 0 || addr < e) {
      continue;
    }
    // Once we are beyond the last changed address,
    // look for a line in our original lines that has a matching address.
    // If we find it, the remaining lines will disassemble the same,
    // and we can just copy them over.
    while (eline < nlines && lines[eline].addr < addr) {
      eline++;
    }
    if (eline < nlines && addr == lines[eline].addr) {
      diff1_end_line = i - 1;
      diff0_end_line = eline - 1;
      while (eline < nlines) {
        new_lines[i++] = lines[eline++];
      }
      break;
    }
  }

  if (DEBUG) {
    fprintf(stderr, "addr change $%04X to $%04X (exclusive)\n", s, e);
    fprintf(stderr, "changed lines: %d,%d --> %d,%d\n", diff0_start_line,
            diff0_end_line, diff1_start_line, diff1_end_line);
    if (diff1_end_line - diff1_start_line < 5) {
      for (int j = diff0_start_line; j <= diff0_end_line; j++) {
        fprintf(stderr, "	%s\n", lines[j].disasm.full);
      }
      fprintf(stderr, "changed to\n");
      for (int j = diff1_start_line; j <= diff1_end_line; j++) {
        fprintf(stderr, "	%s\n", new_lines[j].disasm.full);
      }
    }
  }

  free(lines);
  lines = new_lines;
  nlines = i;

  Buffer b = {};
  for (int i = diff1_start_line; i <= diff1_end_line; i++) {
    bprintf(&b, "%s\n", lines[i].disasm.full);
  }
  win_fmt_addr(disasm_win, "%d,%d", diff0_start_line + 1, diff0_end_line + 1);
  win_write_data(disasm_win, b.size, b.data);
  free(b.data);
  return new_line;
}

static void highlight_pc_line() {
  uint16_t addr = g.cpu.ir == HALT ? g.cpu.pc : g.cpu.pc - 1;
  int line = find_disasm_line(addr);
  if (line >= 0 && line < nlines && lines[line].addr != addr) {
    line = split_disasm_line(line, addr);
  }
  win_fmt_addr(disasm_win, "%d", line + 1);
  win_fmt_ctl(disasm_win, "clean\ndot=addr\nshow\n");
}

static void update_disasm_win() {
  update_disasm_lines();
  highlight_pc_line();
}

static void close_disasm_win() { win_fmt_ctl(disasm_win, "delete"); }

static AcmeWin *make_disasm_win() {
  if (acme == NULL) {
    return NULL;
  }
  disasm_win = acme_get_win(acme, "disassembly");
  if (disasm_win == NULL) {
    return NULL;
  }
  win_fmt_ctl(disasm_win, "font %s\n", CODE_FONT);
  update_disasm_win();
  atexit(close_disasm_win);
  return disasm_win;
}

static void print_current_instruction() {
  // IR has already been fetched into PC, so we go back one,
  // except for HALT, which doesn't increment PC.
  Addr pc = g.cpu.ir == HALT ? g.cpu.pc : g.cpu.pc - 1;
  Disasm disasm = disassemble(g.mem, MEM_SIZE, pc);
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
    {.name = "IR", .size = REG8, .r8 = REG_IR},
};

static void do_reg(const char *arg_in) {
  char arg[LINE_MAX] = {};
  for (int i = 0; i < strlen(arg_in); i++) {
    arg[i] = toupper(arg_in[i]);
  }
  for (int i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
    if (strcmp(arg, regs[i].name) != 0) {
      continue;
    }
    if (regs[i].size == REG8) {
      uint8_t x = get_reg8(&g.cpu, regs[i].r8);
      printf("%s:%d ($%02X)\n", arg, x, x);
    } else {
      uint16_t x = get_reg16(&g.cpu, regs[i].r16);
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

static void do_dump() {
  static const int NCOL = 3;
  int col = 0;
  for (int i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
    if (regs[i].size == REG8) {
      uint8_t x = get_reg8(&g.cpu, regs[i].r8);
      printf("%s:  %-5d ($%02X)  ", regs[i].name, x, x);
    } else {
      uint16_t x = get_reg16(&g.cpu, regs[i].r16);
      printf("%2s: %-5d ($%04X)", regs[i].name, x, x);
    }
    printf(col == NCOL - 1 ? "\n" : "\t");
    col = (col + 1) % NCOL;
  }
  printf("IME: $%01X %-11s IF: $%02X             IE: $%02X\n", g.cpu.ime,
         g.cpu.ei_pend ? "(pend)" : "      ", g.mem[MEM_IF], g.mem[MEM_IE]);
}

static void do_peek(const char *arg_in) {
  char arg[LINE_MAX] = {};
  for (int i = 0; i < strlen(arg_in); i++) {
    arg[i] = toupper(arg_in[i]);
  }

  for (const MemName *n = mem_names; n->name != NULL; n++) {
    if (strcmp(arg, n->name) == 0) {
      uint8_t x = g.mem[n->addr];
      printf("%s ($%04X): %d ($%02X)\n", n->name, n->addr, x, x);
      return;
    }
  }

  int addr = 0;
  if (sscanf(arg_in, "%d", &addr) != 1 && sscanf(arg_in, "$%x", &addr) != 1) {
    printf("Invalid peek: %s\n", arg_in);
    printf("Expected a named location, decimal, or $hex address\n");
    printf("Available named locations are: ");
    for (const MemName *n = mem_names; n->name != NULL; n++) {
      if (n != mem_names) {
        printf(", ");
      }
      printf("%s", n->name);
    }
    printf("\n");
    return;
  }
  if (addr < 0 || addr > 0xFFFF) {
    printf("Invalid address %d ($%04X), must be in range 0-0xFFFF\n", addr,
           addr);
    return;
  }
  uint8_t x = g.mem[addr];
  for (const MemName *n = mem_names; n->name != NULL; n++) {
    if (n->addr == addr) {
      printf("%s ($%04X): %d ($%02X)\n", n->name, n->addr, x, x);
      return;
    }
  }
  printf("$%04X: %d ($%02X)\n", addr, x, x);
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
    return "#";
  default:
    fail("impossible pixel value %d\n", px);
  }
  return ""; // impossible
}

static void poll_lcd_event_thread(void *unused) {
  if (!win_start_events(lcd_win)) {
    fprintf(stderr, "failed to start events: %s\n", errstr9());
  }
  for (;;) {
    AcmeEvent *event = win_wait_event(lcd_win);
    if (event->type == 0) {
      fprintf(stderr, "event error: %s\n", event->data);
      free(event);
      break;
    }
    if (event->type == 'x') {
      mutex_lock9(&mtx);
      if (strcmp(event->data, "Up") == 0) {
        g.dpad |= BUTTON_UP;
        button_count = BUTTON_TIME;
      } else if (strcmp(event->data, "Down") == 0) {
        g.dpad |= BUTTON_DOWN;
        button_count = BUTTON_TIME;
      } else if (strcmp(event->data, "Left") == 0) {
        g.dpad |= BUTTON_LEFT;
        button_count = BUTTON_TIME;
      } else if (strcmp(event->data, "Right") == 0) {
        g.dpad |= BUTTON_RIGHT;
        button_count = BUTTON_TIME;
      } else if (strcmp(event->data, "AButton") == 0) {
        g.buttons |= BUTTON_A;
        button_count = BUTTON_TIME;
      } else if (strcmp(event->data, "BButton") == 0) {
        g.buttons |= BUTTON_B;
        button_count = BUTTON_TIME;
      } else if (strcmp(event->data, "Start") == 0) {
        g.buttons |= BUTTON_START;
        button_count = BUTTON_TIME;
      } else if (strcmp(event->data, "Select") == 0) {
        g.buttons |= BUTTON_SELECT;
        button_count = BUTTON_TIME;
      } else if (strcmp(event->data, "Break") == 0) {
        go = false;
      } else if (strcmp(event->data, "Del") == 0 ||
                 strcmp(event->data, "Delete") == 0) {
        free(event);
        mutex_unlock9(&mtx);
        break;
      } else {
        win_write_event(lcd_win, event);
      }
      mutex_unlock9(&mtx);
    } else if (event->type == 'X' || event->type == 'l' || event->type == 'L' ||
               event->type == 'r' || event->type == 'R') {
      win_write_event(lcd_win, event);
    }
    free(event);
  }
  exit(0);
}

static void check_button_count() {
  if (button_count == 0) {
    return;
  }
  button_count--;
  if (button_count == 0) {
    g.buttons = 0;
    g.dpad = 0;
  }
}

static void print_vram(Buffer *b, const char *font) {
  AcmeWin *vram_win = acme_get_win(acme, "vram");
  if (vram_win == NULL) {
    printf("Failed to open Acme win %s\n", "vram");
    return;
  }
  if (win_fmt_addr(vram_win, ",") < 0) {
    printf("error writing to vram win addr: %s\n", errstr9());
  }
  if (win_write_data(vram_win, b->size, b->data) < 0) {
    printf("error writing to vram win data: %s\n", errstr9());
  }
  if (win_fmt_addr(vram_win, "#0") < 0) {
    printf("error writing to vram win addr: %s\n", errstr9());
  }
  if (win_fmt_ctl(vram_win, "font %s\nclean\ndot=addr\nshow\n", font) < 0) {
    printf("error writing to vram win ctl: %s\n", errstr9());
  }
  win_release(vram_win);
}

enum { MAX_TILE_INDEX = 384 };

static void do_tile(int tile_index) {
  if (tile_index < 0 || tile_index > MAX_TILE_INDEX) {
    printf("tile index must be between 0 and %d\n", MAX_TILE_INDEX);
    return;
  }
  Buffer b = {};
  bprintf(&b, "tile %d:\n", tile_index);
  uint16_t addr = MEM_TILE_BLOCK0_START + tile_index * 16;
  bprintf(&b, "$%04X-$%04X: ", addr, addr + 16 - 1);
  for (int i = 0; i < 16; i++) {
    bprintf(&b, "%02X ", g.mem[addr + i]);
  }
  bprintf(&b, "\n");
  bprintf(&b, "+--------+\n");
  for (int y = 0; y < 8; y++) {
    bprintf(&b, "|");
    uint8_t row_low = g.mem[addr++];
    uint8_t row_high = g.mem[addr++];
    for (int x = 0; x < 8; x++) {
      uint8_t px_low = row_low >> (7 - x) & 1;
      uint8_t px_high = row_high >> (7 - x) & 1;
      bprintf(&b, "%s", px_str(px_high << 1 | px_low));
    }
    bprintf(&b, "|\n");
  }
  bprintf(&b, "+--------+\n");

  print_vram(&b, TILE_FONT);
  free(b.data);
}

static void do_tilemap() {
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
        uint8_t row_low = g.mem[addr + y * 2];
        uint8_t row_high = g.mem[addr + y * 2 + 1];
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
  print_vram(&b, VRAM_MAP_FONT);
  free(b.data);
}

static void do_bgmap(int map_index) {
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
        int tile = g.mem[map_addr + (32 * map_y) + map_x];
        uint16_t addr = MEM_TILE_BLOCK0_START + tile * 16;
        uint8_t row_low = g.mem[addr + y * 2];
        uint8_t row_high = g.mem[addr + y * 2 + 1];
        for (int x = 0; x < 8; x++) {
          uint8_t px_low = row_low >> (7 - x) & 1;
          uint8_t px_high = row_high >> (7 - x) & 1;
          bprintf(&b, "%s", px_str(px_high << 1 | px_low));
        }
      }
      bprintf(&b, "\n");
    }
  }
  print_vram(&b, VRAM_MAP_FONT);
  free(b.data);
}

static int first_line_diff(uint8_t a[SCREEN_HEIGHT][SCREEN_WIDTH],
                           uint8_t b[SCREEN_HEIGHT][SCREEN_WIDTH]) {
  for (int y = 0; y < SCREEN_HEIGHT; y++) {
    if (memcmp(a[y], b[y], SCREEN_WIDTH) != 0) {
      return y;
    }
  }
  return SCREEN_HEIGHT;
}

static int last_line_diff(uint8_t a[SCREEN_HEIGHT][SCREEN_WIDTH],
                          uint8_t b[SCREEN_HEIGHT][SCREEN_WIDTH]) {
  for (int y = SCREEN_HEIGHT - 1; y >= 0; y--) {
    if (memcmp(a[y], b[y], SCREEN_WIDTH) != 0) {
      return y;
    }
  }
  return -1;
}

static double time_ns() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1000000000 + (double)ts.tv_nsec;
}

static void draw_thread(void *unused) {
  static Buffer b;
  static bool first = true;
  static uint8_t cur[SCREEN_HEIGHT][SCREEN_WIDTH] = {};
  static uint8_t latest[SCREEN_HEIGHT][SCREEN_WIDTH] = {};
  double last = time_ns();
  for (;;) {
    double since = time_ns() - last;
    if (since < FRAME_NS) {
      struct timespec ts = {.tv_nsec = FRAME_NS - since};
      nanosleep(&ts, NULL);
    }
    last = time_ns();

    mutex_lock9(&mtx);
    memcpy(latest, lcd, sizeof(lcd));
    mutex_unlock9(&mtx);

    int start_y;
    int end_y;
    if (first) {
      start_y = 0;
      end_y = SCREEN_HEIGHT - 1;
    } else {
      start_y = first_line_diff(cur, latest);
      if (start_y >= SCREEN_HEIGHT) {
        continue;
      }
      end_y = last_line_diff(cur, latest);
    }
    for (int y = start_y; y <= end_y; y++) {
      memcpy(cur[y], latest[y], SCREEN_WIDTH);
    }

    b.size = 0;
    for (int y = start_y; y <= end_y; y++) {
      for (int x = 0; x < SCREEN_WIDTH; x++) {
        bprintf(&b, "%s", px_str(latest[y][x]));
      }
      bprintf(&b, "\n");
    }

    int n = 0;
    if (start_y == 0 && end_y == SCREEN_HEIGHT - 1) {
      n = win_fmt_addr(lcd_win, ",");
      first = false;
    } else {
      n = win_fmt_addr(lcd_win, "%d,%d", start_y + 1, end_y + 1);
    }
    if (n < 0) {
      printf("error writing to lcd win addr: %s\n", errstr9());
      continue;
    }
    if (win_write_data(lcd_win, b.size, b.data) < 0) {
      printf("error writing to lcd win data: %s\n", errstr9());
    }
  }
}

static void draw_lcd() {
  mutex_lock9(&mtx);
  memcpy(lcd, g.lcd, sizeof(lcd));
  mutex_unlock9(&mtx);
}

static void close_lcd_win() { win_fmt_ctl(lcd_win, "delete"); }

static AcmeWin *make_lcd_win() {
  if (acme == NULL) {
    return NULL;
  }
  lcd_win = acme_get_win(acme, "lcd");
  if (lcd_win == NULL) {
    return NULL;
  }
  win_fmt_ctl(lcd_win, "cleartag\nfont %s\n", VRAM_MAP_FONT);
  win_fmt_tag(lcd_win, " Break\n        Up"
                       "\nLeft         Right            AButton        Start"
                       "\n      Down                    BButton        Select");

  static Thread9 poll_thrd;
  thread_create9(&poll_thrd, poll_lcd_event_thread, NULL);
  static Thread9 draw_thrd;
  thread_create9(&draw_thrd, draw_thread, NULL);
  atexit(close_lcd_win);
  return lcd_win;
}

static void do_step(int n) {
  if (n < 0) {
    printf("step argument must be positive\n");
    return;
  }
  step = n;
  go = true;
}

static void check_step() {
  if (step == 0) {
    return;
  }
  step--;
  if (step == 0) {
    go = false;
  }
}

static void do_next() {
  next_sp = g.cpu.sp;
  go = true;
}

static void check_next() {
  if (next_sp >= 0 && g.cpu.sp == next_sp) {
    next_sp = -1;
    go = false;
  }
}

static void do_break_n(int n) {
  if (n < 0 || n > 0xFFFF) {
    printf("break argument must be in the range 0-$FFFF\n");
    return;
  }
  if (nbreaks == MAX_BREAKS) {
    printf("max breaks (%d) already reached\n", MAX_BREAKS);
    return;
  }
  for (int i = 0; i < nbreaks; i++) {
    if (breaks[i] == n) {
      memmove(breaks + i, breaks + i + 1, nbreaks - i - 1);
      nbreaks--;
      printf("Removed break point $%04X\n", n);
      return;
    }
  }
  breaks[nbreaks++] = n;
  printf("Set break point $%04X\n", n);
}

static void do_break() {
  printf("Break points:\n");
  for (int i = 0; i < nbreaks; i++) {
    printf("\t$%04X\n", breaks[i]);
  }
}

static void check_break() {
  for (int i = 0; i < nbreaks; i++) {
    if (breaks[i] == g.cpu.pc - 1) {
      go = false;
    }
  }
}

static void check_cpu_break_point() {
  if (g.break_point) {
    go = false;
    g.break_point = false;
  }
}

// Returns whether to step the next instruction.
static bool handle_input_line() {
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
    do_reg(arg_s);
  } else if (sscanf(line, "peek %s", arg_s) == 1) {
    do_peek(arg_s);
  } else if (sscanf(line, "tile %d", &arg_d) == 1) {
    do_tile(arg_d);
  } else if (strcmp(line, "tilemap") == 0) {
    do_tilemap();
  } else if (sscanf(line, "bgmap %d", &arg_d) == 1) {
    do_bgmap(arg_d);
  } else if (strcmp(line, "dump") == 0) {
    do_dump();
  } else if (sscanf(line, "step %d", &arg_d) == 1) {
    do_step(arg_d);
  } else if (strcmp(line, "next") == 0) {
    do_next();
  } else if (sscanf(line, "break $%x", &arg_d) == 1) {
    do_break_n(arg_d);
  } else if (strcmp(line, "break") == 0) {
    do_break();
  } else if (strcmp(line, "go") == 0) {
    go = true;
  } else if (strcmp(line, "quit") == 0) {
    exit(0);
  }
  return true;
}

static void print_exiting() { printf("exiting\n"); }

int main(int argc, const char *argv[]) {
  if (argc != 2) {
    fail("Expected 1 argument, got %d", argc);
  }
  atexit(print_exiting);

  signal(SIGINT, sigint_handler);

  mutex_init9(&mtx);
  Rom rom = read_rom(argv[1]);
  printf("Loaded ROM file %s\n", argv[1]);
  printf("File Size: %d bytes\n", rom.size);
  printf("Title: %s\n", rom.title);
  printf("Type: %s\n", cart_type_string(rom.cart_type));
  printf("ROM size: %d\n", rom.rom_size);
  printf("ROM banks: %d\n", rom.num_rom_banks);
  printf("RAM size: %d\n", rom.ram_size);
  g = init_gameboy(&rom);

  acme = acme_connect();
  if (acme == NULL) {
    printf("Failed to connect to Acme. Acme integration disabled.\n");
  }
  lcd_win = make_lcd_win();
  if (lcd_win == NULL) {
    printf("Failed to open LCD win: %s\n", errstr9());
  }
  disasm_win = make_disasm_win();
  if (disasm_win == NULL) {
    printf("Failed to open disassembly win: %s\n", errstr9());
  }

  double last_vblank = time_ns();
  long num_mcycle = 0;
  double mcycle_ns_avg = 0;
  for (;;) {
    if (!go && (g.cpu.state == DONE || g.cpu.state == HALTED)) {
      if (num_mcycle > 0) {
        printf("num mcycles: %ld\navg time: %lf ns\n", num_mcycle,
               mcycle_ns_avg);
        num_mcycle = 0;
      }
      update_disasm_win();
      print_current_instruction();
      while (!go && handle_input_line()) {
      }
    }

    PpuMode prev_ppu_mode = ppu_mode(&g);
    double start_ns = time_ns();
    mutex_lock9(&mtx);
    mcycle(&g);
    check_button_count();
    mutex_unlock9(&mtx);
    long ns = time_ns() - start_ns;

    if (ppu_mode(&g) == VBLANK && prev_ppu_mode != VBLANK) {
      draw_lcd();
      double since = time_ns() - last_vblank;
      if (since < VBLANK_NS) {
        struct timespec ts = {.tv_nsec = VBLANK_NS - since};
        nanosleep(&ts, NULL);
      }
      last_vblank = time_ns();
    }

    if (go) {
      if (num_mcycle == 0) {
        mcycle_ns_avg = ns;
      } else {
        mcycle_ns_avg = mcycle_ns_avg + (ns - mcycle_ns_avg) / (num_mcycle + 1);
      }
      num_mcycle++;
      check_step();
      check_next();
      check_break();
      check_cpu_break_point();
      if (go) {
        continue;
      }
    }
  }
  free_rom(&rom);
  return 0;
}
