#ifndef GAMEBOY_H
#define GAMEBOY_H

#include <stdint.h>

// Aborts with a message printf-style message.
extern void fail(const char *fmt, ...);

enum { MEM_SIZE = 0xFFFF };

typedef struct {
  uint8_t a, b, c, d, e, f, h, l;
  uint16_t sp, pc;
} Cpu;

typedef struct {
  Cpu cpu;
} Gameboy;

// An opaque handle to an instruction.
struct instruction;
typedef struct instruction Instruction;

// Returns the size of the instruction in bytes.
extern int instr_size(const Instruction *instr);

// Decodes the instruction at addr in mem.
extern const Instruction *instr_decode(const uint8_t mem[MEM_SIZE],
                                       uint16_t addr);

// Writes a human readable version of the instruction at addr in mem
// to out, writing no more than size bytes including the '\0' terminator.
// The return value is the decoded instruction.
extern const Instruction *
instr_snprint(char *out, int size, const uint8_t mem[MEM_SIZE], uint16_t addr);

#endif // GAMEBOY_H
