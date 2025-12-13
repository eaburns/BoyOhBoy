#include "gameboy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAIL(...)                                                              \
  do {                                                                         \
    fprintf(stderr, "%s: ", __func__);                                         \
    fail(__VA_ARGS__);                                                         \
  } while (0)

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

struct ppu_test {
  const char *name;
  Gameboy init;
  const Gameboy want;
  int cycles;
};

static void _run_ppu_test(const char *name, int n, struct ppu_test tests[]) {
  for (int i = 0; i < n; i++) {
    for (int j = 0; j < tests[i].cycles; j++) {
      ppu_tcycle(&tests[i].init);
    }
    char *diff = gameboy_diff(&tests[i].init, &tests[i].want);
    if (diff != NULL) {
      FAIL("%s %s: diff:\n%s\n", name, tests[i].name, diff);
    }
  }
}

static void run_stopped_test() {
  static struct ppu_test tests[] = {
      {
          .name = "stop",
          .init =
              {
                  .mem =
                      {
                          [MEM_LYC] = 5,
                          [MEM_LCDC] = 0, /* PPU stopped */
                          [MEM_STAT] = DRAWING,
                      },
                  .ppu = {.mode = DRAWING, .ticks = 10},
              },
          .want =
              {
                  .mem =
                      {
                          [MEM_LYC] = 5,
                          [MEM_LCDC] = 0,
                      },
                  .ppu = {.mode = OAM_SCAN, .ticks = 0},
              },
          .cycles = 1,
      },
      {
          // TODO: is this even the correct behavior?
          .name = "stop resets LY=LYC bit",
          .init =
              {
                  .mem =
                      {
                          [MEM_LYC] = 0,
                          [MEM_LY] = 5,
                          [MEM_LCDC] = 0, /* PPU stopped */
                          [MEM_STAT] = DRAWING,
                      },
                  .ppu = {.mode = DRAWING, .ticks = 10},
              },
          .want =
              {
                  .mem =
                      {
                          [MEM_LYC] = 0,
                          [MEM_LY] = 0,
                          [MEM_LCDC] = 0,
                          [MEM_STAT] = STAT_LC_EQ_LYC,
                      },
                  .ppu = {.mode = OAM_SCAN, .ticks = 0},
              },
          .cycles = 1,
      },
  };
  _run_ppu_test(__func__, ARRAY_SIZE(tests), tests);
}

static void run_cycle_count_tests() {
  static struct ppu_test tests[] = {
      {
          .name = "OAM SCAN 78 cycles",
          .init =
              {
                  .mem = {[MEM_LCDC] = LCDC_ENABLED},
                  .ppu = {.mode = OAM_SCAN, .ticks = 0},
              },
          .want =
              {
                  .mem =
                      {
                          [MEM_LCDC] = LCDC_ENABLED,
                          [MEM_STAT] = OAM_SCAN,
                      },
                  .ppu = {.mode = OAM_SCAN, .ticks = 78},
              },
          .cycles = 78,
      },
      {
          // Ticks 0-79 are OAM_SCAN, and after the 79th tick, we move to
          // DRAWING.
          .name = "OAM SCAN 79 cycles",
          .init =
              {
                  .mem = {[MEM_LCDC] = LCDC_ENABLED},
                  .ppu = {.mode = OAM_SCAN, .ticks = 0},
              },
          .want =
              {
                  .mem =
                      {
                          [MEM_LCDC] = LCDC_ENABLED,
                          [MEM_STAT] = DRAWING,
                      },
                  .ppu = {.mode = DRAWING, .ticks = 0},
              },
          .cycles = 79,
      },
      {
          .name = "DRAWING 170 cycles",
          .init =
              {
                  .mem = {[MEM_LCDC] = LCDC_ENABLED},
                  .ppu = {.mode = DRAWING, .ticks = 0},
              },
          .want =
              {
                  .mem =
                      {
                          [MEM_LCDC] = LCDC_ENABLED,
                          [MEM_STAT] = DRAWING,
                      },
                  .ppu = {.mode = DRAWING, .ticks = 170},
              },
          .cycles = 170,
      },
      {
          // For the time being, we just have a fixed 171 cycle DRAWING mode.
          .name = "DRAWING 171 cycles",
          .init =
              {
                  .mem = {[MEM_LCDC] = LCDC_ENABLED},
                  .ppu = {.mode = DRAWING, .ticks = 0},
              },
          .want =
              {
                  .mem =
                      {
                          [MEM_LCDC] = LCDC_ENABLED,
                          [MEM_STAT] = HBLANK,
                      },
                  .ppu = {.mode = HBLANK, .ticks = 0},
              },
          .cycles = 171,
      },
      {
          .name = "HBLANK 202 cycles",
          .init =
              {
                  .mem = {[MEM_LCDC] = LCDC_ENABLED},
                  .ppu = {.mode = HBLANK, .ticks = 0},
              },
          .want =
              {
                  .mem =
                      {
                          [MEM_LCDC] = LCDC_ENABLED,
                          [MEM_STAT] = HBLANK,
                      },
                  .ppu = {.mode = HBLANK, .ticks = 202},
              },
          .cycles = 202,
      },
      {
          // For the time being, we have a fixed HBLANK of 203 cycles.
          .name = "HBLANK 203 cycles",
          .init =
              {
                  .mem = {[MEM_LCDC] = LCDC_ENABLED},
                  .ppu = {.mode = HBLANK, .ticks = 0},
              },
          .want =
              {
                  .mem =
                      {
                          [MEM_LY] = 1,
                          [MEM_LCDC] = LCDC_ENABLED,
                          [MEM_STAT] = OAM_SCAN,
                      },
                  .ppu = {.mode = OAM_SCAN, .ticks = 0},
              },
          .cycles = 203,
      },
      {
          .name = "HBLANK enter VBLANK",
          .init =
              {
                  .mem = {[MEM_LY] = 143, [MEM_LCDC] = LCDC_ENABLED},
                  .ppu = {.mode = HBLANK, .ticks = 0},
              },
          .want =
              {
                  .mem =
                      {
                          [MEM_LY] = 144,
                          [MEM_LCDC] = LCDC_ENABLED,
                          [MEM_STAT] = VBLANK,
                          [MEM_IF] = IF_VBLANK,
                      },
                  .ppu = {.mode = VBLANK, .ticks = 0},
              },
          .cycles = 203,
      },
  };
  _run_ppu_test(__func__, ARRAY_SIZE(tests), tests);
}

int main() {
  run_stopped_test();
  run_cycle_count_tests();

  return 0;
}