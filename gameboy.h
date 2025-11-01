#ifndef GAMEBOY_H
#define GAMEBOY_H

#include <stdint.h>

// Aborts with a message printf-style message.
extern void fail(const char *fmt, ...);

enum { MEM_SIZE = 0xFFFF };

struct cpu {
  uint8_t a, b, c, d, e, f, h, l;
  uint16_t sp, pc;
};

struct gameboy {
  struct cpu cpu;
};

enum operand {
  NONE = 0,

  // Register operands.
  A,
  SP,
  HL,
  CMEM, // [C]
  SP_PLUS_IMM8,

  // Operands encoded into the first byte of the instruction.
  R16,    // 2 bits
  R16STK, // 2 bits
  R16MEM, // 2 bits
  R8,     // 3 bits
  COND,   // 2 bits
  TGT3,   // 3 bits

  // BIT_INDEX and R8_DST are to handle special cases
  // for the small number of instructions that encode 2 arguments
  // into the opcode. Both of them get the opcode at shift+3.
  // The other argument is at shift.
  BIT_INDEX, // 3 bits, always at shift+3.
  R8_DST,    // 3 bits, always at shift+3

  // Immediate values following the first byte of the instruction.
  IMM8,
  IMM8_OFFSET, // 2s complement signed address offset
  IMM8MEM,     // [imm8]
  IMM16,
  IMM16MEM, // [imm16]
};

struct instr {
  // The instruction mnemonic. For example "LD".
  const char *mnemonic;

  // If cb_prefix is true, this is a 2-byte op code.
  // The first byte is 0xCB, and the following byte
  // contains op_code as normal.
  bool cb_prefix;

  // The first byte of the instruction
  // (2nd byte in the case of cb_prefix==true), but with 0
  // in the place of any operands encoded into the byte.
  uint8_t op_code;

  // Instructions can have 0, 1, or 2 operands.
  // If the instruction has more than one operand,
  // one of the operands is always an immediate value
  // that follows the first byte of the instruction.
  enum operand operand1, operand2;

  // If one of the operands is encoded into the 1st byte of the instruction,
  // this indicates the number of bits to right-shift to find the operand.
  int shift;

  // Executes the next cycle of the instruction.
  // Returns whether the instruction is complete.
  bool (*exec)(struct gameboy *, struct instr *, int cycle);
};

// Returns the size of the instruction in bytes.
extern int instr_size(const struct instr *instr);

// Decodes the instruction at addr in mem.
extern const struct instr *instr_decode(const uint8_t mem[MEM_SIZE],
                                        uint16_t addr);

// Writes a human readable version of the instruction at addr in mem
// to out, writing no more than size bytes including the '\0' terminator.
// The return value is the decoded instruction.
extern const struct instr *
instr_snprint(char *out, int size, const uint8_t mem[MEM_SIZE], uint16_t addr);

#endif // GAMEBOY_H