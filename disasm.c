#include "gameboy.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, const char *argv[]) {
  if (argc != 2) {
    fail("expected 1 argument, got %d", argc);
  }

  int rom_size = 0;
  Rom rom = read_rom(argv[1]);
  printf("rom size: %d (bytes)\n", rom.size);

  Addr addr = 0;
  while (addr < rom.size) {
    Disasm disasm = disassemble(rom.data, addr);
    printf("%s\n", disasm.full);
    addr += disasm.size;
  }
}
