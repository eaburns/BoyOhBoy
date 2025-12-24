#include "gb/gameboy.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, const char *argv[]) {
  if (argc != 2 && argc != 3) {
    fail("expected 1 or 2 arguments, got %d", argc);
  }
  uint16_t start_addr = 0;
  if (argc == 3) {
    char *end = NULL;
    long l = strtol(argv[2], &end, 16);
    if (*end != '\0') {
      printf("bad starting address %s\n", argv[2]);
      return 1;
    }
    if (l < 0 || l > 0xFFFF) {
      printf("address %s is out-of-range; must be between 0-FFFF\n", argv[2]);
      return 1;
    }
    start_addr = l;
  }

  int rom_size = 0;
  Rom rom = read_rom(argv[1]);
  printf("rom size: %d (bytes)\n", rom.size);

  Addr addr = start_addr;
  while (addr < rom.size) {
    Disasm disasm = disassemble(rom.data, rom.size, addr);
    printf("%s\n", disasm.full);
    addr += disasm.size;
  }
}
