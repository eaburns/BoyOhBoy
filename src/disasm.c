#include "9/acme.h"
#include "9/errstr.h"
#include "gb/gameboy.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
} DisasmWin;

static void update_from_line(DisasmWin *win, int i, uint16_t align_start_addr) {
  int addr = win->lines[i].addr;
  int orig_nlines = win->nlines;
  win->nlines = i;
  while (addr < win->data_size) {
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
      fail("instruction went off the end of the data");
    }
    addr += line->instr.size;
  }
}

void init_disasm_win(DisasmWin *win, const uint8_t *data, int data_size) {
  win->data_size = data_size;
  win->data = data;
  update_from_line(win, 0, win->data_size);
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

void update_disasm_win(DisasmWin *win, uint16_t start_addr,
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

void jump_disasm_win(DisasmWin *win, uint16_t addr) {
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

static void redraw_disasm_win(DisasmWin *dwin, AcmeWin *awin) {
  int start = 0;
  for (; start < dwin->nlines && !dwin->lines[start].dirty; start++)
    ;
  int end = dwin->nlines - 1;
  for (; end > start && !dwin->lines[end].dirty; end--)
    ;

  int n = 0;
  int size = 1024;
  char *buf = calloc(size, sizeof(*buf));
  for (int i = start; i <= end; i++) {
    DisasmLine *line = &dwin->lines[i];
    line->dirty = false;
    int line_len = strlen(line->instr.full) + sizeof('\n');
    if (n + line_len >= size) {
      while (n + line_len >= size) {
        size *= 2;
      }
      buf = realloc(buf, size);
    }
    strcpy(buf + n, line->instr.full);
    n += line_len;
    buf[n - 1] = '\n';
  }
  win_fmt_addr(awin, "%d,%d", start + 1, end + 1);
  win_write_data(awin, n, buf);
  free(buf);
  win_fmt_addr(awin, "%d", dwin->cur + 1);
  win_fmt_ctl(awin, "dot=addr\nshow\n");
}

static void step(Gameboy *g) {
  do {
    cpu_mcycle(g);
  } while (g->cpu.state == EXECUTING || g->cpu.state == INTERRUPTING);
}

int main(int argc, const char *argv[]) {
  if (argc != 2) {
    fail("expected 1 argument, got %d", argc);
  }

  Rom rom = read_rom(argv[1]);
  Gameboy g = init_gameboy(&rom);

  Acme *acme = acme_connect();
  if (acme == NULL) {
    fprintf(stderr, "acme_connect failed: %s\n", errstr9());
    return 1;
  }
  AcmeWin *awin = acme_get_win(acme, "+disasm");
  if (awin == NULL) {
    fprintf(stderr, "failed to open win: %s\n", errstr9());
    return 1;
  }
  win_fmt_ctl(awin, "font /mnt/font/GoMono/11a/font\n");
  DisasmWin dwin;
  init_disasm_win(&dwin, g.mem, MEM_SIZE);
  step(&g); // step over the initial NOP
  for (;;) {
    jump_disasm_win(&dwin, g.cpu.pc - 1);
    redraw_disasm_win(&dwin, awin);
    step(&g);
  }

  acme_close(acme);
  return 0;
}
