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
    printf("%04x: ", addr);
    char buf[INSTRUCTION_STR_MAX];
    const Instruction *instr =
        format_instruction(buf, sizeof(buf), rom.data, addr);
    int size = instruction_size(instr);
    switch (size) {
    case 1:
      printf("%02x      ", rom.data[addr]);
      break;
    case 2:
      printf("%02x %02x   ", rom.data[addr], rom.data[addr + 1]);
      break;
    case 3:
      printf("%02x %02x %02x", rom.data[addr], rom.data[addr + 1],
             rom.data[addr + 2]);
      break;
    }
    printf("		%s\n", buf);
    addr += size;
  }
}
