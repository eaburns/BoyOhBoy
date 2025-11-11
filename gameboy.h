#ifndef GAMEBOY_H
#define GAMEBOY_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// Aborts with a message printf-style message.
void fail(const char *fmt, ...);

typedef struct {
  const uint8_t *data;
  int size;
} Rom;

// Reads and returns the Rom at path.
// If there is an error reading the file, fail() is called.
// The memory allocated for the returned Rom
// can be freed with free_rom();
Rom read_rom(const char *path);

// Frees any memory allocated for the Rom.
void free_rom(Rom *rom);

enum {
  MEM_ROM_START = 0x0000,
  MEM_ROM_END = 0x7FFF,

  // Rom bank 0.
  MEM_ROM0_START = 0,
  MEM_ROM0_END = 0x3FFF,

  // Rom bank N (depending which is mapped in.)
  MEM_ROMN_START = 0x4000,
  MEM_ROMN_END = MEM_ROM_END,

  // Video RAM.
  MEM_VRAM_START = 0x8000,
  MEM_VRAM_END = 0x9FFF,

  // RAM on the cart.
  MEM_EXT_RAM_START = 0xA000,
  MEM_EXT_RAM_END = 0xBFFF,

  // Working RAM.
  MEM_WRAM_START = 0xC000,
  MEM_WRAM_END = 0xDFFF,

  // Echo RAM (reads 0xC000-0xDDFF).
  MEM_ECHO_RAM_START = 0xE000,
  MEM_ECHO_RAM_END = 0xFDFF,

  // Object Attribute Memory.
  MEM_OAM_START = 0xFE00,
  MEM_OAM_END = 0xFE9F,

  MEM_PROHIBITED_START = 0xFEA0,
  MEM_PROHIBITED_END = 0xFEFF,

  // Various mem-mappend I/O.
  MEM_IO_START = 0xFF00,
  // The memory address of the IF (interrupts pending) flags.
  MEM_IF = 0xFF0F,
  MEM_IO_END = 0xFF7F,

  // High RAM.
  MEM_HIGH_RAM_START = 0xFF80,
  MEM_HIGH_RAM_END = 0xFFFE,

  // The memory address of the IE (interrupts enabled) flags.
  MEM_IE = 0xFFFF,

  MEM_SIZE = 0x10000,
};

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

typedef enum {
  FLAG_Z = 1 << 7,
  FLAG_N = 1 << 6,
  FLAG_H = 1 << 5,
  FLAG_C = 1 << 4,
} Flag;

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
const Instruction *format_instruction(char *out, int size, const uint8_t *data,
                                      uint16_t offs);

typedef enum {
  // An instruction just finished, and we have fetch IR for the next
  // instruction.
  DONE,
  // An instruction is in the middle of executing.
  EXECUTING,
  // The CPU is in the middle of calling an interrupt.
  INTERRUPTING,
  // The CPU is halted.
  HALTED,
} CpuState;

// Returns the name of the state.
const char *cpu_state_name(CpuState s);

typedef struct {
  // The 8-bit registers, indexed by the Reg8 enum.
  // Note that value at index REG_DL_MEM is always 0,
  // since that is not an actual 8-bit register.
  uint8_t registers[8];
  uint8_t flags, ir;
  uint16_t sp, pc;
  bool ime, ei_pend;
  CpuState state;

  // The current instruction bank, either instructions or cb_instructions.
  const Instruction *bank;
  // The current instruction in ir.
  const Instruction *instr;
  // The number of cycles spent so far executing ir.
  int cycle;
  // Scratch space used by instruction execution to hold state between cycles.
  uint8_t w, z;
} Cpu;

// Returns the string name of the given register.
const char *reg8_name(Reg8 r);
const char *reg16_name(Reg16 r);
const char *cond_name(Cond c);

// Get or set the value of the register.
// It is an error to get or set REG_HL_MEM.
uint8_t get_reg8(const Cpu *cpu, Reg8 r);
void set_reg8(Cpu *cpu, Reg8 r, uint8_t x);

// Get or set the value of the register.
// Getting or setting REG_HL_PLUS and REG_HL_MINUS are equivalent to using
// REG_HL.
uint16_t get_reg16(const Cpu *cpu, Reg16 r);
void set_reg16_low_high(Cpu *cpu, Reg16 r, uint8_t low, uint8_t high);
void set_reg16(Cpu *cpu, Reg16 r, uint16_t x);

typedef struct {
  Cpu cpu;
  Mem mem;
  const Rom *rom;
} Gameboy;

// Returns a new Gameboy for the given Rom.
// The Gameboy maintains a pointer to rom,
// so rom must outlive the use of the returned Gameboy.
Gameboy init_gameboy(const Rom *rom);

// Executes a single "M cycle" of the CPU.
void cpu_mcycle(Gameboy *g);

// Returns whether two Gameboy states are equal.
bool gameboy_eq(const Gameboy *a, const Gameboy *b);

// Prints the difference between two Gameboy states to f.
void gameboy_print_diff(FILE *f, const Gameboy *a, const Gameboy *b);

#endif // GAMEBOY_H
