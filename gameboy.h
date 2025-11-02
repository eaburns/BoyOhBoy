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

// The maximum snprint_instruction size in bytes among all instructions,
// including the \0 terminator. This can be used to allocate the buffer passed
// to snprint_instruction to ensure no truncation.
//
// (Really, the max is 24 or 25? I forgot, but let's round up to a nice 32.)
enum { INSTRUCTION_STR_MAX = 32 };

// Writes a human readable version of the instruction at addr in mem
// to out, writing at most size bytes including the '\0' terminator.
// The decoded instruction is returned.
const Instruction *format_instruction(char *out, int size, const Mem mem, Addr addr);

#endif // GAMEBOY_H
