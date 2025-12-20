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

static const char *TILE_FONT = "/mnt/font/GoMono/11a/font";
static const char *VRAM_MAP_FONT = "/mnt/font/GoMono-Bold/3a/font";

static Mutex9 g_mtx;
static Gameboy g;
static Acme *acme = NULL;

static int step = 0;

static int next_sp = -1;

enum { MAX_BREAKS = 10 };
static int nbreaks = 0;
static int breaks[MAX_BREAKS];

static sig_atomic_t go = false;
static sig_atomic_t done = false;

// Guarded by g_mtx.
static bool poll_thread_exited = false;

// Guarded by g_mtx;
enum { BUTTON_TIME = 10000 };
static int button_count;

enum { LINE_MAX = 128 };

void sigint_handler(int s) {
  if (go) {
    printf("\n");
    go = false;
  } else {
    done = true;
  }
}

typedef struct {
  uint16_t addr;
  Disasm instr;
  bool dirty;
} DisasmLine;

enum { MAX_DISASM_LINES = MEM_SIZE };

// A disassembly window.
typedef struct {
  int cur;
  int nlines;
  DisasmLine lines[MAX_DISASM_LINES];
  int data_size;
  const uint8_t *data;
  Mem mem;
  AcmeWin *win;
} DisasmWin;

static void update_from_line(DisasmWin *win, int i, uint16_t align_start_addr) {
  int addr = win->lines[i].addr;
  int orig_nlines = win->nlines;
  win->nlines = i;
  while (addr < win->data_size - 1) {
    if (win->nlines >= MAX_DISASM_LINES) {
      fail("too many instructions (%d >= %d) (addr=%d, data_size=%d\n",
           win->nlines, MAX_DISASM_LINES, addr, win->data_size);
    }
    DisasmLine *line = &win->lines[win->nlines++];
    if (addr >= align_start_addr && win->nlines < orig_nlines &&
        line->addr == addr) {
      // Stop early if we are beyond align_start_addr and we find a line with a
      // matching address. If this occurs it means that we have aligned with a
      // suffix of the original DisasmWin lines that align and will match the
      // rest of the way to orign_nlines.
      win->nlines = orig_nlines;
      break;
    }
    line->dirty = true;
    line->addr = addr;
    line->instr = disassemble(win->data, addr);
    if (addr + line->instr.size > win->data_size) {
      fail("instruction at $%04X + %d went off the end of the data", addr,
           line->instr.size);
    }
    addr += line->instr.size;
  }
}

static int find_addr_line(DisasmWin *win, uint16_t addr) {
  if (addr >= win->data_size) {
    fail("bad addr");
  }
  // TODO: binary search.
  int i = 0;
  while (i < win->nlines &&
         win->lines[i].addr + win->lines[i].instr.size <= addr) {
    i++;
  }
  if (i == win->nlines) {
    fail("impossible line");
  }
  return i;
}

static int mem_diff_start(Mem prev) {
  int a = 0;
  for (; a < MEM_SIZE; a++) {
    if (prev[a] != g.mem[a]) {
      break;
    }
  }
  return a;
}

static int mem_diff_end(Mem prev) {
  int a = MEM_SIZE - 1;
  for (; a >= 0; a--) {
    if (prev[a] != g.mem[a]) {
      break;
    }
  }
  return a;
}

static void update_disasm_win(DisasmWin *win, uint16_t start_addr,
                              uint16_t end_excl_addr) {
  if (start_addr > end_excl_addr) {
    fail("bad start addr before end addr");
  }
  if (end_excl_addr > win->data_size) {
    fail("bad changed addr");
  }
  int i = find_addr_line(win, start_addr);
  update_from_line(win, i, end_excl_addr);
}

static void jump_disasm_win(DisasmWin *win, uint16_t addr) {
  int i = find_addr_line(win, addr);
  if (win->lines[i].addr == addr) {
    win->cur = i;
    return;
  }

  // Jumped into the middle of an instruction.
  // Split it into single-byte UNKNOWN instructions.
  int shift = win->lines[i].instr.size;
  for (int j = win->nlines - 1; j > i; j--) {
    win->lines[j + shift] = win->lines[j];
  }
  win->nlines += shift;
  for (int j = 0; j < shift; j++) {
    DisasmLine *line = &win->lines[i + j];
    line->addr = win->lines[i].addr + j;
    if (line->addr == addr) {
      win->cur = i + j;
    }
    // TODO: if disassemble had a byte limit, we could disasm
    // size 1 to get an UNKNOWN. Instead we fake one here.
    strcpy(line->instr.instr, "UNKNOWN");
    snprintf(line->instr.full, sizeof(line->instr.full),
             "%04x: %02x      		UNKNOWN", line->addr,
             win->data[line->addr]);
    line->instr.size = 1;
  }

  // Start disassembling from the address we jumped to that was previously in
  // the middle of an instruction and now should be one of the UNKNOWN
  // instruction bytes.
  update_from_line(win, win->cur, 0);
}

static void redraw_disasm_win(DisasmWin *win) {
  int start = 0;
  for (; start < win->nlines && !win->lines[start].dirty; start++)
    ;
  int end = win->nlines - 1;
  for (; end > start && !win->lines[end].dirty; end--)
    ;

  Buffer b = {};
  for (int i = start; i <= end; i++) {
    DisasmLine *line = &win->lines[i];
    line->dirty = false;
    bprintf(&b, "%s\n", line->instr.full);
  }
  win_fmt_addr(win->win, "%d,%d", start + 1, end + 1);
  win_write_data(win->win, b.size, b.data);
  win_fmt_addr(win->win, "%d", win->cur + 1);
  win_fmt_ctl(win->win, "clean\ndot=addr\nshow\n");
  free(b.data);
}

static void update_code_win(DisasmWin *win) {
  if (win->win == NULL) {
    return;
  }
  int diff_start = mem_diff_start(win->mem);
  if (diff_start < MEM_SIZE - 1) {
    int diff_end = mem_diff_end(win->mem);
    update_disasm_win(win, diff_start, diff_end);
    memcpy(win->mem + diff_start, g.mem + diff_start, diff_end - diff_start);
  }
  jump_disasm_win(win, g.cpu.pc - 1);
  redraw_disasm_win(win);
}

static void print_current_instruction() {
  static const uint16_t HALT = 0x76;
  // IR has already been fetched into PC, so we go back one,
  // except for HALT, which doesn't increment PC.
  Addr pc = g.cpu.ir == HALT ? g.cpu.pc : g.cpu.pc - 1;
  Disasm disasm = disassemble(g.mem, pc);
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
}

// Various named memory locations.
static const struct {
  const char *name;
  uint16_t addr;
} mems[] = {
    {"P1_JOYPAD", MEM_P1_JOYPAD},
    {"JOYP", MEM_P1_JOYPAD},
    {"P1", MEM_P1_JOYPAD},
    {"JOYPAD", MEM_P1_JOYPAD},
    {"DIV", MEM_DIV},
    {"TIMA", MEM_TIMA},
    {"TMA", MEM_TMA},
    {"TAC", MEM_TAC},
    {"IF", MEM_IF},
    {"LCDC", MEM_LCDC},
    {"STAT", MEM_STAT},
    {"SCX", MEM_SCX},
    {"SCY", MEM_SCY},
    {"LY", MEM_LY},
    {"LYC", MEM_LYC},
    {"DMA", MEM_DMA},
    {"BGP", MEM_BGP},
    {"OBP0", MEM_OBP0},
    {"OBP1", MEM_OBP1},
    {"IE", MEM_IE},
};

static void do_peek(const char *arg_in) {
  char arg[LINE_MAX] = {};
  for (int i = 0; i < strlen(arg_in); i++) {
    arg[i] = toupper(arg_in[i]);
  }
  for (int i = 0; i < sizeof(mems) / sizeof(mems[0]); i++) {
    if (strcmp(arg, mems[i].name) == 0) {
      uint8_t x = g.mem[mems[i].addr];
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
  uint8_t x = g.mem[addr];
  for (int i = 0; i < sizeof(mems) / sizeof(mems[0]); i++) {
    if (mems[i].addr == addr) {
      printf("%s ($%04X): %d ($%02X)\n", mems[i].name, mems[i].addr, x, x);
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
    return "0";
  default:
    fail("impossible pixel value %d\n", px);
  }
  return ""; // impossible
}

static void poll_events(void *arg) {
  AcmeWin *lcd_win = arg;
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
      mutex_lock9(&g_mtx);
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
        mutex_unlock9(&g_mtx);
        break;
      } else {
        win_write_event(lcd_win, event);
      }
      mutex_unlock9(&g_mtx);
    } else if (event->type == 'X' || event->type == 'l' || event->type == 'L' ||
               event->type == 'r' || event->type == 'R') {
      win_write_event(lcd_win, event);
    }
    free(event);
  }
  mutex_lock9(&g_mtx);
  poll_thread_exited = true;
  mutex_unlock9(&g_mtx);
}

static bool lcd_deleted() {
  mutex_lock9(&g_mtx);
  bool d = poll_thread_exited;
  mutex_unlock9(&g_mtx);
  return d;
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

enum { NS_PER_MS = 1000000 };

static void draw_lcd(AcmeWin *lcd_win) {
  if (lcd_win == NULL) {
    return;
  }
  static double last_frame = 0;
  double now = time_ns();
  if (now - last_frame < 17 * NS_PER_MS) {
    struct timespec ts = {.tv_nsec = 17 * NS_PER_MS - (now - last_frame)};
    nanosleep(&ts, NULL);
  }
  last_frame = now;

  static Buffer b;
  static bool first = true;
  static uint8_t cur[SCREEN_HEIGHT][SCREEN_WIDTH] = {};

  int start_y;
  int end_y;
  if (first) {
    start_y = 0;
    end_y = SCREEN_HEIGHT - 1;
  } else {
    start_y = first_line_diff(cur, g.lcd);
    if (start_y >= SCREEN_HEIGHT) {
      return;
    }
    end_y = last_line_diff(cur, g.lcd);
  }
  for (int y = start_y; y <= end_y; y++) {
    memcpy(cur[y], g.lcd[y], SCREEN_WIDTH);
  }

  b.size = 0;
  for (int y = start_y; y <= end_y; y++) {
    for (int x = 0; x < SCREEN_WIDTH; x++) {
      bprintf(&b, "%s", px_str(g.lcd[y][x]));
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
    return;
  }
  if (win_write_data(lcd_win, b.size, b.data) < 0) {
    printf("error writing to lcd win data: %s\n", errstr9());
  }
  if (win_fmt_addr(lcd_win, "#0") < 0) {
    printf("error writing to lcd win addr: %s\n", errstr9());
  }
  if (win_fmt_ctl(lcd_win, "font %s\nclean\ndot=addr\nshow\n", VRAM_MAP_FONT) <
      0) {
    printf("error writing to lcd win ctl: %s\n", errstr9());
  }
}

static AcmeWin *make_lcd_win() {
  if (acme == NULL) {
    return NULL;
  }
  AcmeWin *win = acme_get_win(acme, "lcd");
  if (win == NULL) {
    return NULL;
  }
  win_fmt_ctl(win, "cleartag\n");
  win_fmt_tag(win, " Break\n        Up"
                   "\nLeft         Right            AButton        Start"
                   "\n      Down                    BButton        Select");
  return win;
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
    done = true;
  }
  return true;
}

int main(int argc, const char *argv[]) {
  if (argc != 2) {
    fail("Expected 1 argument, got %d", argc);
  }

  signal(SIGINT, sigint_handler);

  mutex_init9(&g_mtx);
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

  AcmeWin *lcd_win = make_lcd_win();
  if (lcd_win == NULL) {
    printf("Failed to open LCD win: %s\n", errstr9());
  } else {
    static Thread9 poll_thrd;
    thread_create9(&poll_thrd, poll_events, lcd_win);
    draw_lcd(lcd_win);
  }

  DisasmWin code_win = {
      .data_size = MEM_SIZE,
      .data = g.mem,
      .win = acme != NULL ? acme_get_win(acme, "code") : NULL,
  };
  if (code_win.win == NULL) {
    printf("Failed to open code win: %s\n", errstr9());
  } else {
    update_from_line(&code_win, 0, code_win.data_size);
  }

  long num_mcycle = 0;
  double mcycle_ns_avg = 0;
  while (!done && !lcd_deleted()) {
    if (!go && g.cpu.state == DONE) {
      if (num_mcycle > 0) {
        printf("num mcycles: %ld\navg time: %lf ns\n", num_mcycle,
               mcycle_ns_avg);
        num_mcycle = 0;
      }
      print_current_instruction();
      update_code_win(&code_win);
      while (!go && !done && handle_input_line()) {
      }
    }

    double start_ns = time_ns();
    PpuMode prev_ppu_mode = ppu_mode(&g);
    mutex_lock9(&g_mtx);
    mcycle(&g);
    check_button_count();
    mutex_unlock9(&g_mtx);
    if (ppu_mode(&g) == VBLANK && prev_ppu_mode != VBLANK) {
      draw_lcd(lcd_win);
    }
    long ns = time_ns() - start_ns;

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
  if (lcd_win != NULL) {
    win_fmt_ctl(lcd_win, "delete");
  }
  if (code_win.win != NULL) {
    win_fmt_ctl(code_win.win, "delete\n");
  }
  free_rom(&rom);
  return 0;
}
