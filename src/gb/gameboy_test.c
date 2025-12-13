#include "gameboy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAIL(...)                                                              \
  do {                                                                         \
    fprintf(stderr, "%s: ", __func__);                                         \
    fail(__VA_ARGS__);                                                         \
  } while (0)

static void run_lcd_diff_test0() {
  Gameboy a = {
      .lcd =
          {
              {0, 0, 1, 1, 1, 0, 0, 0},
              {0, 0, 1, 1, 1, 0, 0, 0},
              {0, 0, 1, 1, 1, 0, 0, 0},
              {0, 0, 1, 1, 1, 0, 0, 0},
          },
  };
  Gameboy b = {
      .lcd =
          {
              {0, 0, 1, 1, 1, 0, 0, 0},
              {0, 0, 1, 1, 2, 0, 0, 0},
              {0, 0, 2, 1, 1, 0, 0, 0},
              {0, 0, 1, 1, 2, 0, 0, 0},
          },
  };
  char *diff = gameboy_diff(&a, &b);
  if (diff == NULL) {
    FAIL("no diff, but expected a diff");
  }
  const char *want = "LCD diff\n"
                     "       2   3   4\n"
                     "    +--------------\n"
                     "  1 |  1   1  1≠2\n"
                     "  2 | 1≠2  1   1 \n"
                     "  3 |  1   1  1≠2\n";
  if (strcmp(diff, want) != 0) {
    FAIL("got\n%s\nwanted\n%s\n", diff, want);
  }
  free(diff);
}

static void run_lcd_diff_test1() {
  Gameboy a = {
      .lcd =
          {
              [98] = {[100] = 1, [101] = 1, [102] = 1},
              [99] = {[100] = 1, [101] = 1, [102] = 1},
              [100] = {[100] = 1, [101] = 1, [102] = 1},
              [101] = {[100] = 1, [101] = 1, [102] = 1},
          },
  };
  Gameboy b = {
      .lcd =
          {
              [98] = {[100] = 1, [101] = 1, [102] = 1},
              [99] = {[100] = 1, [101] = 1, [102] = 2},
              [100] = {[100] = 2, [101] = 1, [102] = 1},
              [101] = {[100] = 1, [101] = 1, [102] = 2},
          },
  };
  char *diff = gameboy_diff(&a, &b);
  if (diff == NULL) {
    FAIL("no diff, but expected a diff");
  }
  const char *want = "LCD diff\n"
                     "     100 101 102\n"
                     "    +--------------\n"
                     " 99 |  1   1  1≠2\n"
                     "100 | 1≠2  1   1 \n"
                     "101 |  1   1  1≠2\n";
  if (strcmp(diff, want) != 0) {
    FAIL("got\n%s\nwanted\n%s\n", diff, want);
  }
  free(diff);
}

int main() {
  run_lcd_diff_test0();
  run_lcd_diff_test1();
  return 0;
}