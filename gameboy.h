#ifndef GAMEBOY_H
#define GAMEBOY_H

#include <stdint.h>

// Aborts with a message printf-style message.
void fail(const char *fmt, ...);

enum { MEM_SIZE = 0xFFFF };

typedef uint8_t Mem[MEM_SIZE];

typedef uint16_t Addr;

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

// A place-holder to represent an unknown instruction.
extern const Instruction *unknown_instruction;

// The primary instruction bank.
extern const Instruction *instructions;

// The bank of instructions that follow a 0xCB byte.
extern const Instruction *cb_instructions;

// Returns the size of the instruction in bytes.
int instruction_size(const Instruction *instr);

// Returns the instruction corresponding to op_code in the given instruction
// bank.
const Instruction *find_instruction(const Instruction *bank, uint8_t op_code);

// Writes a human readable version of the instruction at addr in mem
// to out, writing no more than size bytes including the '\0' terminator.
// The return value is the decoded instruction.
const Instruction *snprint_instruction(char *out, int size, const Mem mem,
                                       Addr addr);

#endif // GAMEBOY_H
