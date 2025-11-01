#ifndef GAMEBOY_H
#define GAMEBOY_H

#include <stdint.h>

// Aborts with a message printf-style message.
extern void fail(const char *fmt, ...);

struct gameboy {
  uint8_t a, b, c, d, e, f, h, l;
  uint16_t sp, pc;
};

struct instruction_tmpl;

// An instruction.
struct instruction {
  // The size of the instruction in bytes.
  int size;

  // A copy of the instruction's object code.
  uint8_t data[3];

  // The instruction's template.
  // This is internal to instruction.c.
  // If template==NULL, this is an unknown instruction,
  // its size is 1 byte, and data[0] is the byte.
  const struct instruction_tmpl *template;
};

// Decodes the first instruction in data,
// reading no more than size bytes.
// fail()s if the data is bad.
extern struct instruction decode(const uint8_t *data, int size);

// Prints a formatted instruction string of up to size bytes
// (including the 0 terminator) to buf.
extern void snprint_instruction(char *buf, int size,
                                const struct instruction *instr);

#endif // GAMEBOY_H