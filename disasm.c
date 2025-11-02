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
  const char *path = argv[1];

  Mem mem;
  FILE *in = fopen(path, "r");
  if (in == NULL) {
    fail("failed to open %s: %s", path, strerror(errno));
  }
  int rom_size = fread(mem, 1, MEM_SIZE, in);
  if (ferror(in)) {
    fail("failed to read from %s: %s", path, strerror(errno));
  }
  if (fclose(in) != 0) {
    fail("failed to close %s: %s", path, strerror(errno));
  }
  printf("rom size: %d (bytes)\n", rom_size);

  Addr addr = 0;
  while (addr < rom_size) {
    printf("%04x: ", addr);
    char buf[INSTRUCTION_STR_MAX];
    const Instruction *instr = format_instruction(buf, sizeof(buf), mem, addr);
    int size = instruction_size(instr);
    switch (size) {
    case 1:
      printf("%02x      ", mem[addr]);
      break;
    case 2:
      printf("%02x %02x   ", mem[addr], mem[addr + 1]);
      break;
    case 3:
      printf("%02x %02x %02x", mem[addr], mem[addr + 1], mem[addr + 2]);
      break;
    }
    printf("		%s\n", buf);
    addr += size;
  }
}
