#include "gameboy.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_rom(const char *path, int *size) {
  FILE *in = fopen(path, "r");
  if (in == NULL) {
    fail("failed to open %s: %s", path, strerror(errno));
  }
  *size = 0;
  uint8_t *rom = NULL;
  uint8_t buf[512];
  for (;;) {
    size_t n = fread(buf, 1, sizeof(buf), in);
    if (n < 0) {
      break;
    }
    rom = realloc(rom, *size + n);
    memcpy(rom + *size, buf, n);
    *size += n;
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
  return rom;
}

int main(int argc, const char *argv[]) {
  if (argc != 2) {
    fail("expected 1 argument, got %d", argc);
  }
  int rom_size = 0;
  uint8_t *rom = read_rom(argv[1], &rom_size);

  printf("rom size: %d (bytes)\n", rom_size);

  int i = 0;
  while (i < rom_size) {
    printf("%04x: ", i);
    struct instruction instr = decode(rom + i, rom_size - i);
    i += instr.size;
    if (instr.size == 1) {
      printf("%02x      ", instr.data[0]);
    }
    if (instr.size == 2) {
      printf("%02x %02x   ", instr.data[0], instr.data[1]);
    }
    if (instr.size == 3) {
      printf("%02x %02x %02x", instr.data[0], instr.data[1], instr.data[2]);
    }
    char buf[64];
    buf[0] = 0;
    snprint_instruction(buf, sizeof(buf), &instr);
    printf("		%s\n", buf);
  }
}