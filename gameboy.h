#ifndef GAMEBOY_H
#define GAMEBOY_H

#include <stdint.h>

// Aborts with a message printf-style message.
void fail(const char *fmt, ...);

enum { MEM_SIZE = 0xFFFF };

typedef uint8_t Mem[MEM_SIZE];

typedef uint16_t Addr;

// The 8-bit registers.
typedef enum {
  REG_B = 0,
  REG_C = 1,
  REG_D = 2,
  REG_E = 3,
  REG_H = 4,
  REG_L = 5,
  REG_HL_MEM = 6, // [HL]
  REG_A = 7,
} Reg8;

// The 16-bit registers.
typedef enum {
  // These first four match their encoded form in an op-code.
  REG_BC = 0,
  REG_DE = 1,
  REG_HL = 2,
  REG_SP = 3,

  // These do not match their encoded form in an op-code.
  // They share numbers with the above as listed below.
  REG_AF,       // 3
  REG_HL_PLUS,  // 2
  REG_HL_MINUS, // 3
} Reg16;

typedef enum {
  NZ = 0,
  Z = 1,
  NC = 2,
  C = 3,
} Cond;

typedef struct {
  // The 8-bit registers, indexed by the Reg8 enum.
  // Note that value at index REG_DL_MEM is always 0,
  // since that is not an actual 8-bit register.
  uint8_t registers[8];
  uint16_t sp, pc;
} Cpu;

// Returns the string name of the given register.
const char *reg8_name(Reg8 r);
const char *reg16_name(Reg16 r);
const char *cond_name(Cond c);

// Get or set the value of the register.
uint8_t get_reg8(const Cpu *cpu, Reg8 r);
void set_reg8(Cpu *cpu, Reg8 r, uint8_t x);

// Get or set the value of the register.
// Only supports REG_BC, REG_DE, REG_HL, and REG_SP. Any other Reg16 will fail.
uint16_t get_reg16(const Cpu *cpu, Reg16 r);
void set_reg16(Cpu *cpu, Reg16 r, uint8_t low, uint8_t high);

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
const Instruction *format_instruction(char *out, int size, const Mem mem,
                                      Addr addr);

#endif // GAMEBOY_H
