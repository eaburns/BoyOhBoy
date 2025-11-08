#include "gameboy.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef enum {
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
} Operand;

struct instruction {
  // The instruction mnemonic. For example "LD".
  const char *mnemonic;

  // The instruction op code.
  uint8_t op_code;

  // Instructions can have 0, 1, or 2 operands.
  // If the instruction has more than one operand,
  // one of the operands is always an immediate value
  // that follows the first byte of the instruction.
  Operand operand1, operand2;

  // If one of the operands is encoded into the 1st byte of the instruction,
  // this indicates the number of bits to right-shift to find the operand.
  int shift;

  // Executes the next cycle of the instruction.
  // Returns whether the instruction is complete.
  ExecResult (*exec)(Gameboy *, const Instruction *, int cycle);
};

// Reads the byte at the given memory address.
// CPU emulation should always read memory using fetch or one of the variants
// that call into fetch instead of accessing memory directly. This is because
// fetch takes care of situations were certain memory is not actually readable
// by the CPU.
static uint8_t fetch(const Gameboy *g, Addr addr) {
  // TODO: actually deal with cases where memory is not readable by the CPU.
  return g->mem[addr];
}

// Fetches the byte at the PC register and increments it.
static uint8_t fetch_pc(Gameboy *g) { return fetch(g, g->cpu.pc++); }

// Writes the byte to the given memory address.
// CPU emulation should always write memory using store instead of accessing
// memory directly. This is because store takes care of situations were certain
// memory is not actually writable by the CPU.
void store(Gameboy *g, Addr addr, uint8_t x) {
  // TODO: actually deal with cases where memory is not writable by the CPU.
  g->mem[addr] = x;
}

ExecResult cpu_mcycle(Gameboy *g) {
  Cpu *cpu = &g->cpu;

  if (cpu->ir == 0xCB) {
    cpu->ir = fetch_pc(g);
    cpu->cycle++;
    cpu->bank = cb_instructions;
    cpu->instr = NULL; // should already be null, but just in case.
    return NOT_DONE;
  }

  if (cpu->bank == NULL) {
    cpu->bank = instructions;
  }
  if (cpu->instr == NULL) {
    cpu->instr = find_instruction(cpu->bank, cpu->ir);
  }
  ExecResult result = cpu->instr->exec(g, cpu->instr, cpu->cycle);
  cpu->cycle++;
  if (result == DONE) {
    cpu->bank = instructions;
    cpu->instr = NULL;
    cpu->cycle = 0;
    memset(cpu->scratch, 0, sizeof(cpu->scratch));
  }
  return result;
}

static Reg8 decode_reg8(int shift, uint8_t op_code) {
  return (op_code >> shift) & 0x7;
}

static Reg8 decode_reg8_dst(int shift, uint8_t op_code) {
  return (op_code >> (shift + 3)) & 0x7;
}

static Reg16 decode_reg16(int shift, uint8_t op_code) {
  return (op_code >> shift) & 0x3;
}

static Reg16 decode_reg16stk(int shift, uint8_t op_code) {
  Reg16 r = decode_reg16(shift, op_code);
  return r == 3 ? REG_AF : r;
}

static Reg16 decode_reg16mem(int shift, uint8_t op_code) {
  Reg16 r = decode_reg16(shift, op_code);
  return r == 2 ? REG_HL_PLUS : (r == 3 ? REG_HL_MINUS : r);
}

static int decode_bit_index(int shift, uint8_t op_code) {
  return (op_code >> (shift + 3)) & 0x7;
}

static Cond decode_cond(int shift, uint8_t op_code) {
  return (op_code >> shift) & 0x3;
}

static int decode_tgt3(int shift, uint8_t op_code) {
  return ((op_code >> shift) & 0x7) * 8;
}

static void assign_flag(Cpu *cpu, Flag f, bool value) {
  if (value) {
    cpu->flags |= f;
  } else {
    cpu->flags &= ~f;
  }
}

static bool get_flag(const Cpu *cpu, Flag f) { return cpu->flags & f; }

// Returns whether adding x+y would half-carry.
static bool add_half_carries(uint8_t x, uint8_t y) {
  return ((x & 0xF) + (y & 0xF)) >> 4;
}

// Returns whether adding x+y+z would half-carry.
static bool add3_half_carries(uint8_t x, uint8_t y, uint8_t z) {
  return ((x & 0xF) + (y & 0xF) + (z & 0xF)) >> 4;
}

// Returns whether adding x+y would carry.
static bool add_carries(uint8_t x, uint8_t y) { return (x + y) >> 8; }

// Returns whether adding x+y+z would carry.
static bool add3_carries(uint8_t x, uint8_t y, uint8_t z) {
  return (x + y + z) >> 8;
}

// Returns whether x-y borrows.
static bool sub_borrows(uint8_t x, uint8_t y) { return y > x; }

// Returns whether x-y-c borrows.
static bool sub3_borrows(uint8_t x, uint8_t y, uint8_t z) {
  return (y + z) > x;
}

// Returns whether x-y half-borrows.
static bool sub_half_borrows(uint8_t x, uint8_t y) {
  return ((x >> 4) & 1) && !((x - y) >> 4 & 1);
}

// Returns whether x-y-z half-borrows.
static bool sub3_half_borrows(uint8_t x, uint8_t y, uint8_t z) {
  return sub_half_borrows(x, y) || sub_half_borrows(x - y, z);
}

// Adds x to register r, setting flag N to 0 and setting H and C to the
// appropriate carry bits.
static void add_to_reg8(Cpu *cpu, Reg8 r, uint8_t x) {
  uint8_t y = get_reg8(cpu, r);
  set_reg8(cpu, r, x + y);
  assign_flag(cpu, FLAG_N, false);
  assign_flag(cpu, FLAG_H, add_half_carries(x, y));
  assign_flag(cpu, FLAG_C, add_carries(x, y));
}

static ExecResult exec_nop(Gameboy *g, const Instruction *, int cycle) {
  g->cpu.ir = fetch_pc(g);
  return DONE;
}

static ExecResult exec_ld_r16_imm16(Gameboy *g, const Instruction *instr,
                                    int cycle) {
  Cpu *cpu = &g->cpu;
  switch (cycle) {
  case 0:
    cpu->scratch[0] = fetch_pc(g);
    return NOT_DONE;
  case 1:
    cpu->scratch[1] = fetch_pc(g);
    return NOT_DONE;
  default: // 2
    Reg16 r = decode_reg16(instr->shift, cpu->ir);
    set_reg16_low_high(cpu, r, cpu->scratch[0], cpu->scratch[1]);
    cpu->ir = fetch_pc(g);
    return DONE;
  }
}

static ExecResult exec_ld_r16mem_a(Gameboy *g, const Instruction *instr,
                                   int cycle) {
  Cpu *cpu = &g->cpu;
  switch (cycle) {
  case 0:
    Reg16 r = decode_reg16mem(instr->shift, g->cpu.ir);
    Addr addr = get_reg16(cpu, r);
    uint8_t a = get_reg8(cpu, REG_A);
    if (r == REG_HL_PLUS) {
      set_reg16(cpu, r, addr + 1);
    } else if (r == REG_HL_MINUS) {
      set_reg16(cpu, r, addr - 1);
    }
    store(g, addr, a);
    return NOT_DONE;
  default: // 1
    cpu->ir = fetch_pc(g);
    return DONE;
  }
}

static ExecResult exec_ld_a_r16mem(Gameboy *g, const Instruction *instr,
                                   int cycle) {
  Cpu *cpu = &g->cpu;
  switch (cycle) {
  case 0:
    Reg16 r = decode_reg16mem(instr->shift, cpu->ir);
    Addr addr = get_reg16(cpu, r);
    uint8_t x = fetch(g, addr);
    set_reg8(cpu, REG_A, x);
    if (r == REG_HL_PLUS) {
      set_reg16(cpu, r, addr + 1);
    } else if (r == REG_HL_MINUS) {
      set_reg16(cpu, r, addr - 1);
    }
    return NOT_DONE;
  default: // 1
    cpu->ir = fetch_pc(g);
    return DONE;
  }
}

static ExecResult exec_ld_imm16mem_sp(Gameboy *g, const Instruction *instr,
                                      int cycle) {
  Cpu *cpu = &g->cpu;
  uint16_t sp = get_reg16(cpu, REG_SP);
  // Addr is only valid on cycles 2, 3, and 4,
  // since it is still being loaded on cycles 0 and 1.
  Addr addr = (uint16_t)cpu->scratch[1] << 8 | cpu->scratch[0];
  switch (cycle) {
  case 0:
    cpu->scratch[0] = fetch_pc(g);
    return NOT_DONE;
  case 1:
    cpu->scratch[1] = fetch_pc(g);
    return NOT_DONE;
  case 2:
    store(g, addr, sp & 0xFF);
    return NOT_DONE;
  case 3:
    store(g, addr + 1, sp >> 8);
  default: // 4
    cpu->ir = fetch_pc(g);
    return DONE;
  }
}

static ExecResult exec_inc_r16(Gameboy *g, const Instruction *instr,
                               int cycle) {
  Cpu *cpu = &g->cpu;
  switch (cycle) {
  case 0:
    Reg16 r = decode_reg16(instr->shift, cpu->ir);
    set_reg16(cpu, r, get_reg16(cpu, r) + 1);
    return NOT_DONE;
  default: // 1
    cpu->ir = fetch_pc(g);
    return DONE;
  }
}

static ExecResult exec_dec_r16(Gameboy *g, const Instruction *instr,
                               int cycle) {
  Cpu *cpu = &g->cpu;
  switch (cycle) {
  case 0:
    Reg16 r = decode_reg16(instr->shift, cpu->ir);
    set_reg16(cpu, r, get_reg16(cpu, r) - 1);
    return NOT_DONE;
  default: // 1
    cpu->ir = fetch_pc(g);
    return DONE;
  }
}

static ExecResult exec_add_hl_r16(Gameboy *g, const Instruction *instr,
                                  int cycle) {
  Cpu *cpu = &g->cpu;
  Reg16 r = decode_reg16(instr->shift, cpu->ir);
  uint16_t x = get_reg16(cpu, r);
  switch (cycle) {
  case 0:
    add_to_reg8(cpu, REG_L, x & 0xFF);
    return NOT_DONE;
  default: // 1
    add_to_reg8(cpu, REG_H, (x >> 8) + get_flag(cpu, FLAG_C));
    cpu->ir = fetch_pc(g);
    return DONE;
  }
}

static ExecResult exec_inc_r8(Gameboy *g, const Instruction *instr, int cycle) {
  Cpu *cpu = &g->cpu;
  uint8_t x = get_reg8(cpu, REG_A);
  set_reg8(cpu, REG_A, x + 1);
  assign_flag(cpu, FLAG_Z, get_reg8(cpu, REG_A) == 0);
  assign_flag(cpu, FLAG_N, false);
  assign_flag(cpu, FLAG_H, add_half_carries(x, 1));
  cpu->ir = fetch_pc(g);
  return DONE;
}

static ExecResult exec_dec_r8(Gameboy *g, const Instruction *instr, int cycle) {
  Cpu *cpu = &g->cpu;
  uint8_t x = get_reg8(cpu, REG_A);
  set_reg8(cpu, REG_A, x - 1);
  assign_flag(cpu, FLAG_Z, get_reg8(cpu, REG_A) == 0);
  assign_flag(cpu, FLAG_N, true);
  assign_flag(cpu, FLAG_H, sub_half_borrows(x, 1));
  cpu->ir = fetch_pc(g);
  return DONE;
}

static ExecResult exec_ld_r8_imm8(Gameboy *g, const Instruction *instr,
                                  int cycle) {
  Cpu *cpu = &g->cpu;
  switch (cycle) {
  case 0:
    cpu->scratch[0] = fetch_pc(g);
    return NOT_DONE;
  case 1:
    Reg8 r = decode_reg8(instr->shift, cpu->ir);
    if (r == REG_HL_MEM) {
      store(g, get_reg16(cpu, REG_HL), cpu->scratch[0]);
      return NOT_DONE;
    }
    set_reg8(cpu, r, cpu->scratch[0]);
    // FALLTHROUGH
  default: // 2
    cpu->ir = fetch_pc(g);
    return DONE;
  }
}

// Returns RLC x, setting Z, N, H, and C.
uint8_t rlc(Cpu *cpu, uint8_t x) {
  uint8_t result = (x << 1) | (x >> 7);
  assign_flag(cpu, FLAG_Z, result == 0);
  assign_flag(cpu, FLAG_N, false);
  assign_flag(cpu, FLAG_H, false);
  assign_flag(cpu, FLAG_C, x >> 7);
  return result;
}

// Returns RRC x, setting Z, N, H, and C.
uint8_t rrc(Cpu *cpu, uint8_t x) {
  uint8_t result = (x >> 1) | ((x & 1) << 7);
  assign_flag(cpu, FLAG_Z, result == 0);
  assign_flag(cpu, FLAG_N, false);
  assign_flag(cpu, FLAG_H, false);
  assign_flag(cpu, FLAG_C, x & 1);
  return result;
}

// Returns RL x, setting Z, N, H, and C.
uint8_t rl(Cpu *cpu, uint8_t x) {
  uint8_t result = (x << 1) | get_flag(cpu, FLAG_C);
  assign_flag(cpu, FLAG_Z, result == 0);
  assign_flag(cpu, FLAG_N, false);
  assign_flag(cpu, FLAG_H, false);
  assign_flag(cpu, FLAG_C, x >> 7);
  return result;
}

// Returns RR x, setting Z, N, H, and C.
uint8_t rr(Cpu *cpu, uint8_t x) {
  uint8_t result = (x >> 1) | get_flag(cpu, FLAG_C) << 7;
  assign_flag(cpu, FLAG_Z, result == 0);
  assign_flag(cpu, FLAG_N, false);
  assign_flag(cpu, FLAG_H, false);
  assign_flag(cpu, FLAG_C, x & 1);
  return result;
}

static ExecResult exec_rotate_a(Gameboy *g, const Instruction *instr, int cycle,
                                uint8_t (*rotate)(Cpu *, uint8_t)) {
  Cpu *cpu = &g->cpu;
  set_reg8(cpu, REG_A, rotate(cpu, get_reg8(cpu, REG_A)));
  // Register A rotations always set Z to 0 regardless of the result.
  assign_flag(cpu, FLAG_Z, false);
  cpu->ir = fetch_pc(g);
  return DONE;
}

static ExecResult exec_rlca(Gameboy *g, const Instruction *instr, int cycle) {
  return exec_rotate_a(g, instr, cycle, rlc);
}

static ExecResult exec_rrca(Gameboy *g, const Instruction *instr, int cycle) {
  return exec_rotate_a(g, instr, cycle, rrc);
}

static ExecResult exec_rla(Gameboy *g, const Instruction *instr, int cycle) {
  return exec_rotate_a(g, instr, cycle, rl);
}

static ExecResult exec_rra(Gameboy *g, const Instruction *instr, int cycle) {
  return exec_rotate_a(g, instr, cycle, rr);
}

static ExecResult exec_daa(Gameboy *g, const Instruction *instr, int cycle) {
  Cpu *cpu = &g->cpu;
  uint8_t adj = 0;
  uint8_t a = get_reg8(cpu, REG_A);
  if (get_flag(cpu, FLAG_N)) {
    if (get_flag(cpu, FLAG_H)) {
      adj += 0x6;
    }
    if (get_flag(cpu, FLAG_C)) {
      adj += 0x60;
    }
    set_reg8(cpu, REG_A, a - adj);
  } else {
    if (get_flag(cpu, FLAG_H) || (a & 0xF) > 0x9) {
      adj += 0x6;
    }
    if (get_flag(cpu, FLAG_C) || a > 0x99) {
      adj += 0x60;
      assign_flag(cpu, FLAG_C, true);
    }
    set_reg8(cpu, REG_A, a + adj);
  }
  assign_flag(cpu, FLAG_Z, get_reg8(cpu, REG_A) == 0);
  assign_flag(cpu, FLAG_H, 0);
  cpu->ir = fetch_pc(g);
  return DONE;
}

static ExecResult exec_cpl(Gameboy *g, const Instruction *instr, int cycle) {
  Cpu *cpu = &g->cpu;
  set_reg8(cpu, REG_A, ~get_reg8(cpu, REG_A));
  assign_flag(cpu, FLAG_N, true);
  assign_flag(cpu, FLAG_H, true);
  cpu->ir = fetch_pc(g);
  return DONE;
}

static ExecResult exec_scf(Gameboy *g, const Instruction *instr, int cycle) {
  Cpu *cpu = &g->cpu;
  assign_flag(cpu, FLAG_N, false);
  assign_flag(cpu, FLAG_H, false);
  assign_flag(cpu, FLAG_C, true);
  cpu->ir = fetch_pc(g);
  return DONE;
}

static ExecResult exec_ccf(Gameboy *g, const Instruction *instr, int cycle) {
  Cpu *cpu = &g->cpu;
  assign_flag(cpu, FLAG_N, false);
  assign_flag(cpu, FLAG_H, false);
  assign_flag(cpu, FLAG_C, !get_flag(cpu, FLAG_C));
  cpu->ir = fetch_pc(g);
  return DONE;
}

static ExecResult exec_bit_twiddle_r8(Gameboy *g, const Instruction *instr,
                                      int cycle,
                                      uint8_t (*op)(Cpu *, uint8_t)) {
  Cpu *cpu = &g->cpu;
  switch (cycle) {
  case 0:
    fail("impossible cycle 0"); // cycle 0 is reading the 0xCB prefix.
  case 1:
    Reg8 r = decode_reg8(instr->shift, cpu->ir);
    if (r != REG_HL_MEM) {
      set_reg8(cpu, r, op(cpu, get_reg8(cpu, r)));
      cpu->ir = fetch_pc(g);
      return DONE;
    }
    cpu->scratch[0] = fetch(g, get_reg16(cpu, REG_HL));
    return NOT_DONE;
  case 2:
    store(g, get_reg16(cpu, REG_HL), op(cpu, cpu->scratch[0]));
    return NOT_DONE;
  default: // 3
    cpu->ir = fetch_pc(g);
    return DONE;
  }
}

static ExecResult exec_rlc_r8(Gameboy *g, const Instruction *instr, int cycle) {
  return exec_bit_twiddle_r8(g, instr, cycle, rlc);
}

static ExecResult exec_rrc_r8(Gameboy *g, const Instruction *instr, int cycle) {
  return exec_bit_twiddle_r8(g, instr, cycle, rrc);
}

static ExecResult exec_rl_r8(Gameboy *g, const Instruction *instr, int cycle) {
  return exec_bit_twiddle_r8(g, instr, cycle, rl);
}

static ExecResult exec_rr_r8(Gameboy *g, const Instruction *instr, int cycle) {
  return exec_bit_twiddle_r8(g, instr, cycle, rr);
}

// Returns SLA x, setting Z, N,  H, and C.
static uint8_t sla(Cpu *cpu, uint8_t x) {
  uint8_t result = x << 1;
  assign_flag(cpu, FLAG_Z, result == 0);
  assign_flag(cpu, FLAG_N, false);
  assign_flag(cpu, FLAG_H, false);
  assign_flag(cpu, FLAG_C, x >> 7);
  return result;
}

static ExecResult exec_sla_r8(Gameboy *g, const Instruction *instr, int cycle) {
  return exec_bit_twiddle_r8(g, instr, cycle, sla);
}

// Returns SRA x, setting Z, N,  H, and C.
static uint8_t sra(Cpu *cpu, uint8_t x) {
  uint8_t result = x >> 1 | (x & 0x80);
  assign_flag(cpu, FLAG_Z, result == 0);
  assign_flag(cpu, FLAG_N, false);
  assign_flag(cpu, FLAG_H, false);
  assign_flag(cpu, FLAG_C, x & 1);
  return result;
}

static ExecResult exec_sra_r8(Gameboy *g, const Instruction *instr, int cycle) {
  return exec_bit_twiddle_r8(g, instr, cycle, sra);
}

// Returns SWAP x, setting Z, N,  H, and C.
static uint8_t swap_nibbles(Cpu *cpu, uint8_t x) {
  uint8_t result = x >> 4 | (x & 0xFF) << 4;
  assign_flag(cpu, FLAG_Z, result == 0);
  assign_flag(cpu, FLAG_N, false);
  assign_flag(cpu, FLAG_H, false);
  assign_flag(cpu, FLAG_C, false);
  return result;
}

static ExecResult exec_swap_r8(Gameboy *g, const Instruction *instr,
                               int cycle) {
  return exec_bit_twiddle_r8(g, instr, cycle, swap_nibbles);
}

// Returns SRL x, setting Z, N,  H, and C.
static uint8_t srl(Cpu *cpu, uint8_t x) {
  uint8_t result = x >> 1;
  assign_flag(cpu, FLAG_Z, result == 0);
  assign_flag(cpu, FLAG_N, false);
  assign_flag(cpu, FLAG_H, false);
  assign_flag(cpu, FLAG_C, x & 1);
  return result;
}

static ExecResult exec_srl_r8(Gameboy *g, const Instruction *instr, int cycle) {
  return exec_bit_twiddle_r8(g, instr, cycle, srl);
}

static ExecResult exec_bit_b3_r8(Gameboy *g, const Instruction *instr,
                                 int cycle) {
  Cpu *cpu = &g->cpu;
  Reg8 r = decode_reg8(instr->shift, cpu->ir);
  int bit = decode_bit_index(instr->shift, cpu->ir);
  switch (cycle) {
  case 0:
    fail("impossible cycle 0"); // cycle 0 is reading the 0xCB prefix.
  case 1:
    if (r != REG_HL_MEM) {
      assign_flag(cpu, FLAG_Z, (get_reg8(cpu, r) >> bit) ^ 1);
      break;
    }
    cpu->scratch[0] = fetch(g, get_reg16(cpu, REG_HL));
    return NOT_DONE;
  default: // 2
    assign_flag(cpu, FLAG_Z, (cpu->scratch[0] >> bit) ^ 1);
    break;
  }
  assign_flag(cpu, FLAG_N, false);
  assign_flag(cpu, FLAG_H, false);
  cpu->ir = fetch_pc(g);
  return DONE;
}

static ExecResult exec_res_set_b3_r8(Gameboy *g, const Instruction *instr,
                                     int cycle, uint8_t (*op)(int, uint8_t)) {
  Cpu *cpu = &g->cpu;
  Reg8 r = decode_reg8(instr->shift, cpu->ir);
  int bit = decode_bit_index(instr->shift, cpu->ir);
  switch (cycle) {
  case 0:
    fail("impossible cycle 0"); // cycle 0 is reading the 0xCB prefix.
  case 1:
    if (r != REG_HL_MEM) {
      set_reg8(cpu, r, op(bit, get_reg8(cpu, r)));
      cpu->ir = fetch_pc(g);
      return DONE;
    }
    cpu->scratch[0] = fetch(g, get_reg16(cpu, REG_HL));
    return NOT_DONE;
  case 2:
    store(g, get_reg16(cpu, REG_HL), op(bit, cpu->scratch[0]));
    return NOT_DONE;
  default: // 3
    cpu->ir = fetch_pc(g);
    return DONE;
  }
}

uint8_t res_bit(int bit, uint8_t x) { return x & ~(1 << bit); }

static ExecResult exec_res_b3_r8(Gameboy *g, const Instruction *instr,
                                 int cycle) {
  return exec_res_set_b3_r8(g, instr, cycle, res_bit);
}
uint8_t set_bit(int bit, uint8_t x) { return x | (1 << bit); }

static ExecResult exec_set_b3_r8(Gameboy *g, const Instruction *instr,
                                 int cycle) {
  return exec_res_set_b3_r8(g, instr, cycle, set_bit);
}

static ExecResult exec_jr_imm8(Gameboy *g, const Instruction *instr,
                               int cycle) {
  Cpu *cpu = &g->cpu;
  switch (cycle) {
  case 0:
    cpu->scratch[0] = fetch_pc(g);
    return NOT_DONE;
  case 1:
    cpu->pc += (int8_t)cpu->scratch[0];
    return NOT_DONE;
  default: // 2
    cpu->ir = fetch_pc(g);
    return DONE;
  }
}

bool eval_cond(const Cpu *cpu, Cond cc) {
  switch (cc) {
  case NZ:
    return !get_flag(cpu, FLAG_Z);
  case Z:
    return get_flag(cpu, FLAG_Z);
  case NC:
    return !get_flag(cpu, FLAG_C);
  case C:
    return get_flag(cpu, FLAG_C);
  default:
    fail("impossible Cond: %d", cc);
  }
}

static ExecResult exec_jr_cond_imm8(Gameboy *g, const Instruction *instr,
                                    int cycle) {
  Cpu *cpu = &g->cpu;
  switch (cycle) {
  case 0:
    cpu->scratch[0] = fetch_pc(g);
    return NOT_DONE;
  case 1:
    if (!eval_cond(cpu, decode_cond(instr->shift, cpu->ir))) {
      cpu->ir = fetch_pc(g);
      return DONE;
    }
    cpu->pc += (int8_t)cpu->scratch[0];
    return NOT_DONE;
  default: // 2
    cpu->ir = fetch_pc(g);
    return DONE;
  }
}

static ExecResult exec_stop(Gameboy *g, const Instruction *instr, int cycle) {
  fail("STOP instruction is not implemented");
  return DONE; // impossible
}

static ExecResult exec_ld_r8_r8(Gameboy *g, const Instruction *instr,
                                int cycle) {
  Cpu *cpu = &g->cpu;
  Reg8 src = decode_reg8(instr->shift, cpu->ir);
  Reg8 dst = decode_reg8_dst(instr->shift, cpu->ir);

  if (src == REG_HL_MEM && dst == REG_HL_MEM) {
    // LD [HL], [HL] is HALT
    fail("impossible LD [HL], [HL]");
  }

  if (src != REG_HL_MEM && dst != REG_HL_MEM) {
    set_reg8(cpu, dst, get_reg8(cpu, src));
    cpu->ir = fetch_pc(g);
    return DONE;
  }

  if (src == REG_HL_MEM) {
    if (cycle == 0) {
      cpu->scratch[0] = fetch(g, get_reg16(cpu, REG_HL));
      return NOT_DONE;
    }
    set_reg8(cpu, dst, cpu->scratch[0]);
    cpu->ir = fetch_pc(g);
    return DONE;
  }

  // dst == REG_HL_MEM
  if (cycle == 0) {
    store(g, get_reg16(cpu, REG_HL), get_reg8(cpu, src));
    return NOT_DONE;
  }
  cpu->ir = fetch_pc(g);
  return DONE;
}

static ExecResult exec_halt(Gameboy *, const Instruction *, int cycle) {
  // TODO: implement HALT
  fail("HALT is not yet implemented.");
  return DONE; // impossible
}

static ExecResult exec_op_a_r8(Gameboy *g, const Instruction *instr, int cycle,
                               uint8_t (*op)(Cpu *, uint8_t, uint8_t)) {
  Cpu *cpu = &g->cpu;
  Reg8 r = decode_reg8(instr->shift, cpu->ir);
  if (r != REG_HL_MEM) {
    set_reg8(cpu, REG_A, op(cpu, get_reg8(cpu, REG_A), get_reg8(cpu, r)));
    cpu->ir = fetch_pc(g);
    return DONE;
  }

  if (cycle == 0) {
    cpu->scratch[0] = fetch(g, get_reg16(cpu, REG_HL));
    return NOT_DONE;
  }
  set_reg8(cpu, REG_A, op(cpu, get_reg8(cpu, REG_A), cpu->scratch[0]));
  cpu->ir = fetch_pc(g);
  return DONE;
}

static uint8_t add_a(Cpu *cpu, uint8_t a, uint8_t x) {
  uint8_t res = a + x;
  assign_flag(cpu, FLAG_Z, res == 0);
  assign_flag(cpu, FLAG_N, false);
  assign_flag(cpu, FLAG_H, add_half_carries(a, x));
  assign_flag(cpu, FLAG_C, add_carries(a, x));
  return res;
}

static ExecResult exec_add_a_r8(Gameboy *g, const Instruction *instr,
                                int cycle) {
  return exec_op_a_r8(g, instr, cycle, add_a);
}

static uint8_t adc_a(Cpu *cpu, uint8_t a, uint8_t x) {
  uint8_t c = get_flag(cpu, FLAG_C);
  uint8_t res = a + x + c;
  assign_flag(cpu, FLAG_Z, res == 0);
  assign_flag(cpu, FLAG_N, false);
  assign_flag(cpu, FLAG_H, add3_half_carries(a, x, c));
  assign_flag(cpu, FLAG_C, add3_carries(a, x, c));
  return res;
}

static ExecResult exec_adc_a_r8(Gameboy *g, const Instruction *instr,
                                int cycle) {
  return exec_op_a_r8(g, instr, cycle, adc_a);
}

static uint8_t sub_a(Cpu *cpu, uint8_t a, uint8_t x) {
  uint8_t res = a - x;
  assign_flag(cpu, FLAG_Z, res == 0);
  assign_flag(cpu, FLAG_N, true);
  assign_flag(cpu, FLAG_H, sub_half_borrows(a, x));
  assign_flag(cpu, FLAG_C, sub_borrows(a, x));
  return res;
}

static ExecResult exec_sub_a_r8(Gameboy *g, const Instruction *instr,
                                int cycle) {
  return exec_op_a_r8(g, instr, cycle, sub_a);
}

static uint8_t sbc_a(Cpu *cpu, uint8_t a, uint8_t x) {
  uint8_t c = get_flag(cpu, FLAG_C);
  uint8_t res = a - x - c;
  assign_flag(cpu, FLAG_Z, res == 0);
  assign_flag(cpu, FLAG_N, true);
  assign_flag(cpu, FLAG_H, sub3_half_borrows(a, x, c));
  assign_flag(cpu, FLAG_C, sub3_borrows(a, x, c));
  return res;
}

static ExecResult exec_sbc_a_r8(Gameboy *g, const Instruction *instr,
                                int cycle) {
  return exec_op_a_r8(g, instr, cycle, sbc_a);
}

static ExecResult exec_and_a_r8(Gameboy *, const Instruction *, int cycle) {
  return false;
}

static ExecResult exec_xor_a_r8(Gameboy *, const Instruction *, int cycle) {
  return false;
}

static ExecResult exec_or_a_r8(Gameboy *, const Instruction *, int cycle) {
  return false;
}

static ExecResult exec_cp_a_r8(Gameboy *, const Instruction *, int cycle) {
  return false;
}

static ExecResult exec_add_a_imm8(Gameboy *, const Instruction *, int cycle) {
  return false;
}

static ExecResult exec_adc_a_imm8(Gameboy *, const Instruction *, int cycle) {
  return false;
}

static ExecResult exec_sub_a_imm8(Gameboy *, const Instruction *, int cycle) {
  return false;
}

static ExecResult exec_sbc_a_imm8(Gameboy *, const Instruction *, int cycle) {
  return false;
}

static ExecResult exec_and_a_imm8(Gameboy *, const Instruction *, int cycle) {
  return false;
}

static ExecResult exec_xor_a_imm8(Gameboy *, const Instruction *, int cycle) {
  return false;
}

static ExecResult exec_or_a_imm8(Gameboy *, const Instruction *, int cycle) {
  return false;
}

static ExecResult exec_cp_a_imm8(Gameboy *, const Instruction *, int cycle) {
  return false;
}

static ExecResult exec_ret_cond(Gameboy *, const Instruction *, int cycle) {
  return false;
}

static ExecResult exec_ret(Gameboy *, const Instruction *, int cycle) {
  return false;
}

static ExecResult exec_reti(Gameboy *, const Instruction *, int cycle) {
  return false;
}

static ExecResult exec_jp_cond_imm16(Gameboy *, const Instruction *,
                                     int cycle) {
  return false;
}

static ExecResult exec_jp_imm16(Gameboy *, const Instruction *, int cycle) {
  return false;
}

static ExecResult exec_jp_hl(Gameboy *, const Instruction *, int cycle) {
  return false;
}

static ExecResult exec_call_cond_imm16(Gameboy *, const Instruction *,
                                       int cycle) {
  return false;
}

static ExecResult exec_call_imm16(Gameboy *, const Instruction *, int cycle) {
  return false;
}

static ExecResult exec_rst_tgt3(Gameboy *, const Instruction *, int cycle) {
  return false;
}

static ExecResult exec_pop_r16(Gameboy *, const Instruction *, int cycle) {
  return false;
}

static ExecResult exec_push_r16(Gameboy *, const Instruction *, int cycle) {
  return false;
}

static ExecResult exec_ldh_cmem_a(Gameboy *, const Instruction *, int cycle) {
  return false;
}

static ExecResult exec_ldh_imm8mem_a(Gameboy *, const Instruction *,
                                     int cycle) {
  return false;
}

static ExecResult exec_ld_imm16mem_a(Gameboy *, const Instruction *,
                                     int cycle) {
  return false;
}

static ExecResult exec_ldh_a_cmem(Gameboy *, const Instruction *, int cycle) {
  return false;
}

static ExecResult exec_ldh_a_imm8mem(Gameboy *, const Instruction *,
                                     int cycle) {
  return false;
}

static ExecResult exec_ld_a_imm16mem(Gameboy *, const Instruction *,
                                     int cycle) {
  return false;
}

static ExecResult exec_add_sp_imm8(Gameboy *, const Instruction *, int cycle) {
  return false;
}

static ExecResult exec_ld_hl_sp_plus_imm8(Gameboy *, const Instruction *,
                                          int cycle) {
  return false;
}

static ExecResult exec_ld_sp_hl(Gameboy *, const Instruction *, int cycle) {
  return false;
}

static ExecResult exec_di(Gameboy *, const Instruction *, int cycle) {
  return false;
}

static ExecResult exec_ei(Gameboy *, const Instruction *, int cycle) {
  return false;
}

static const Instruction _unknown_instruction = {.mnemonic = "UNKNOWN"};

static const Instruction _instructions[] = {
    {
        .mnemonic = "NOP",
        .op_code = 0x00,
        .exec = exec_nop,
    },
    {
        .mnemonic = "LD",
        .op_code = 0x01,
        .operand1 = R16,
        .shift = 4,
        .operand2 = IMM16,
        .exec = exec_ld_r16_imm16,
    },
    {
        .mnemonic = "LD",
        .op_code = 0x02,
        .operand1 = R16MEM,
        .shift = 4,
        .operand2 = A,
        .exec = exec_ld_r16mem_a,
    },
    {
        .mnemonic = "LD",
        .op_code = 0x0A,
        .operand1 = A,
        .operand2 = R16MEM,
        .shift = 4,
        .exec = exec_ld_a_r16mem,
    },
    {
        .mnemonic = "LD",
        .op_code = 0x08,
        .operand1 = IMM16MEM,
        .operand2 = SP,
        .exec = exec_ld_imm16mem_sp,
    },
    {
        .mnemonic = "INC",
        .op_code = 0x03,
        .operand1 = R16,
        .shift = 4,
        .exec = exec_inc_r16,
    },
    {
        .mnemonic = "DEC",
        .op_code = 0x0B,
        .operand1 = R16,
        .shift = 4,
        .exec = exec_dec_r16,
    },
    {
        .mnemonic = "ADD",
        .op_code = 0x09,
        .operand1 = HL,
        .operand2 = R16,
        .shift = 4,
        .exec = exec_add_hl_r16,
    },
    {
        .mnemonic = "INC",
        .op_code = 0x04,
        .operand1 = R8,
        .shift = 3,
        .exec = exec_inc_r8,
    },
    {
        .mnemonic = "DEC",
        .op_code = 0x05,
        .operand1 = R8,
        .shift = 3,
        .exec = exec_dec_r8,
    },
    {
        .mnemonic = "LD",
        .op_code = 0x06,
        .operand1 = R8,
        .shift = 3,
        .operand2 = IMM8,
        .exec = exec_ld_r8_imm8,
    },
    {
        .mnemonic = "RLCA",
        .op_code = 0x07,
        .exec = exec_rlca,
    },
    {
        .mnemonic = "RRCA",
        .op_code = 0x0F,
        .exec = exec_rrca,
    },
    {
        .mnemonic = "RLA",
        .op_code = 0x17,
        .exec = exec_rla,
    },
    {
        .mnemonic = "RRA",
        .op_code = 0x1F,
        .exec = exec_rra,
    },
    {
        .mnemonic = "DAA",
        .op_code = 0x27,
        .exec = exec_daa,
    },
    {
        .mnemonic = "CPL",
        .op_code = 0x2F,
        .exec = exec_cpl,
    },
    {
        .mnemonic = "SCF",
        .op_code = 0x37,
        .exec = exec_scf,
    },
    {
        .mnemonic = "CCF",
        .op_code = 0x3F,
        .exec = exec_ccf,
    },
    {
        .mnemonic = "JR",
        .op_code = 0x18,
        .operand1 = IMM8_OFFSET,
        .exec = exec_jr_imm8,
    },
    {
        .mnemonic = "JR",
        .op_code = 0x20,
        .operand1 = COND,
        .shift = 3,
        .operand2 = IMM8_OFFSET,
        .exec = exec_jr_cond_imm8,
    },
    {
        .mnemonic = "STOP",
        .op_code = 0x10,
        .operand1 = IMM8,
        .exec = exec_stop,
    },
    {
        .mnemonic = "LD",
        .op_code = 0x40,
        .operand1 = R8_DST,
        .operand2 = R8,
        .shift = 0,
        .exec = exec_ld_r8_r8,
    },
    {
        .mnemonic = "HALT",
        .op_code = 0x76,
        .exec = exec_halt,
    },
    {
        .mnemonic = "ADD",
        .op_code = 0x80,
        .operand1 = A,
        .operand2 = R8,
        .shift = 0,
        .exec = exec_add_a_r8,
    },
    {
        .mnemonic = "ADC",
        .op_code = 0x88,
        .operand1 = A,
        .operand2 = R8,
        .shift = 0,
        .exec = exec_adc_a_r8,
    },
    {
        .mnemonic = "SUB",
        .op_code = 0x90,
        .operand1 = A,
        .operand2 = R8,
        .shift = 0,
        .exec = exec_sub_a_r8,
    },
    {
        .mnemonic = "SBC",
        .op_code = 0x98,
        .operand1 = A,
        .operand2 = R8,
        .shift = 0,
        .exec = exec_sbc_a_r8,
    },
    {
        .mnemonic = "AND",
        .op_code = 0xA0,
        .operand1 = A,
        .operand2 = R8,
        .shift = 0,
        .exec = exec_and_a_r8,
    },
    {
        .mnemonic = "XOR",
        .op_code = 0xA8,
        .operand1 = A,
        .operand2 = R8,
        .shift = 0,
        .exec = exec_xor_a_r8,
    },
    {
        .mnemonic = "OR",
        .op_code = 0xB0,
        .operand1 = A,
        .operand2 = R8,
        .shift = 0,
        .exec = exec_or_a_r8,
    },
    {
        .mnemonic = "CP",
        .op_code = 0xB8,
        .operand1 = A,
        .operand2 = R8,
        .shift = 0,
        .exec = exec_cp_a_r8,
    },
    {
        .mnemonic = "ADD",
        .op_code = 0xC6,
        .operand1 = A,
        .operand2 = IMM8,
        .exec = exec_add_a_imm8,
    },
    {
        .mnemonic = "ADC",
        .op_code = 0xCE,
        .operand1 = A,
        .operand2 = IMM8,
        .shift = 0,
        .exec = exec_adc_a_imm8,
    },
    {
        .mnemonic = "SUB",
        .op_code = 0xD6,
        .operand1 = A,
        .operand2 = IMM8,
        .shift = 0,
        .exec = exec_sub_a_imm8,
    },
    {
        .mnemonic = "SBC",
        .op_code = 0xDE,
        .operand1 = A,
        .operand2 = IMM8,
        .shift = 0,
        .exec = exec_sbc_a_imm8,
    },
    {
        .mnemonic = "AND",
        .op_code = 0xE6,
        .operand1 = A,
        .operand2 = IMM8,
        .shift = 0,
        .exec = exec_and_a_imm8,
    },
    {
        .mnemonic = "XOR",
        .op_code = 0xEE,
        .operand1 = A,
        .operand2 = IMM8,
        .shift = 0,
        .exec = exec_xor_a_imm8,
    },
    {
        .mnemonic = "OR",
        .op_code = 0xF6,
        .operand1 = A,
        .operand2 = IMM8,
        .shift = 0,
        .exec = exec_or_a_imm8,
    },
    {
        .mnemonic = "CP",
        .op_code = 0xFE,
        .operand1 = A,
        .operand2 = IMM8,
        .shift = 0,
        .exec = exec_cp_a_imm8,
    },
    {
        .mnemonic = "RET",
        .op_code = 0xC0,
        .operand1 = COND,
        .shift = 3,
        .exec = exec_ret_cond,
    },
    {
        .mnemonic = "RET",
        .op_code = 0xC9,
        .exec = exec_ret,
    },
    {
        .mnemonic = "RETI",
        .op_code = 0xD9,
        .exec = exec_reti,
    },
    {
        .mnemonic = "JP",
        .op_code = 0xC2,
        .operand1 = COND,
        .shift = 3,
        .operand2 = IMM16,
        .exec = exec_jp_cond_imm16,
    },
    {
        .mnemonic = "JP",
        .op_code = 0xC3,
        .operand1 = IMM16,
        .exec = exec_jp_imm16,
    },
    {
        .mnemonic = "JP",
        .op_code = 0xE9,
        .operand1 = HL,
        .exec = exec_jp_hl,
    },
    {
        .mnemonic = "CALL",
        .op_code = 0xC4,
        .operand1 = COND,
        .shift = 3,
        .operand2 = IMM16,
        .exec = exec_call_cond_imm16,
    },
    {
        .mnemonic = "CALL",
        .op_code = 0xCD,
        .operand1 = IMM16,
        .exec = exec_call_imm16,
    },
    {
        .mnemonic = "RST",
        .op_code = 0xC7,
        .operand1 = TGT3,
        .shift = 3,
        .exec = exec_rst_tgt3,
    },
    {
        .mnemonic = "POP",
        .op_code = 0xC1,
        .operand1 = R16STK,
        .shift = 4,
        .exec = exec_pop_r16,
    },
    {
        .mnemonic = "PUSH",
        .op_code = 0xC5,
        .operand1 = R16STK,
        .shift = 4,
        .exec = exec_push_r16,
    },
    {
        .mnemonic = "LDH",
        .op_code = 0xE2,
        .operand1 = CMEM,
        .operand2 = A,
        .exec = exec_ldh_cmem_a,
    },
    {
        .mnemonic = "LDH",
        .op_code = 0xE0,
        .operand1 = IMM8MEM,
        .operand2 = A,
        .exec = exec_ldh_imm8mem_a,
    },
    {
        .mnemonic = "LD",
        .op_code = 0xEA,
        .operand1 = IMM16MEM,
        .operand2 = A,
        .exec = exec_ld_imm16mem_a,
    },
    {
        .mnemonic = "LDH",
        .op_code = 0xF2,
        .operand1 = A,
        .operand2 = CMEM,
        .exec = exec_ldh_a_cmem,
    },
    {
        .mnemonic = "LDH",
        .op_code = 0xF0,
        .operand1 = A,
        .operand2 = IMM8MEM,
        .exec = exec_ldh_a_imm8mem,
    },
    {
        .mnemonic = "LD",
        .op_code = 0xFA,
        .operand1 = A,
        .operand2 = IMM16MEM,
        .exec = exec_ld_a_imm16mem,
    },
    {
        .mnemonic = "ADD",
        .op_code = 0xE8,
        .operand1 = SP,
        .operand2 = IMM8,
        .exec = exec_add_sp_imm8,
    },
    {
        .mnemonic = "LD",
        .op_code = 0xF8,
        .operand1 = HL,
        .operand2 = SP_PLUS_IMM8,
        .exec = exec_ld_hl_sp_plus_imm8,
    },
    {
        .mnemonic = "LD",
        .op_code = 0xF9,
        .operand1 = SP,
        .operand2 = HL,
        .exec = exec_ld_sp_hl,
    },
    {
        .mnemonic = "DI",
        .op_code = 0xF3,
        .exec = exec_di,
    },
    {
        .mnemonic = "EI",
        .op_code = 0xFB,
        .exec = exec_ei,
    },
    {.mnemonic = NULL /* sentinal value */},
};

static const Instruction _cb_instructions[] = {
    {
        .mnemonic = "RLC",
        .op_code = 0x00,
        .operand1 = R8,
        .shift = 0,
        .exec = exec_rlc_r8,
    },
    {
        .mnemonic = "RRC",
        .op_code = 0x08,
        .operand1 = R8,
        .shift = 0,
        .exec = exec_rrc_r8,
    },
    {
        .mnemonic = "RL",
        .op_code = 0x10,
        .operand1 = R8,
        .shift = 0,
        .exec = exec_rl_r8,
    },
    {
        .mnemonic = "RR",
        .op_code = 0x18,
        .operand1 = R8,
        .shift = 0,
        .exec = exec_rr_r8,
    },
    {
        .mnemonic = "SLA",
        .op_code = 0x20,
        .operand1 = R8,
        .shift = 0,
        .exec = exec_sla_r8,
    },
    {
        .mnemonic = "SRA",
        .op_code = 0x28,
        .operand1 = R8,
        .shift = 0,
        .exec = exec_sra_r8,
    },
    {
        .mnemonic = "SWAP",
        .op_code = 0x30,
        .operand1 = R8,
        .shift = 0,
        .exec = exec_swap_r8,
    },
    {
        .mnemonic = "SRL",
        .op_code = 0x38,
        .operand1 = R8,
        .shift = 0,
        .exec = exec_srl_r8,
    },
    {
        .mnemonic = "BIT",
        .op_code = 0x40,
        .operand1 = BIT_INDEX,
        .operand2 = R8,
        .shift = 0,
        .exec = exec_bit_b3_r8,
    },
    {
        .mnemonic = "RES",
        .op_code = 0x80,
        .operand1 = BIT_INDEX,
        .operand2 = R8,
        .shift = 0,
        .exec = exec_res_b3_r8,
    },
    {
        .mnemonic = "SET",
        .op_code = 0xC0,
        .operand1 = BIT_INDEX,
        .operand2 = R8,
        .shift = 0,
        .exec = exec_set_b3_r8,
    },
    {.mnemonic = NULL, /* sentinal */},
};

const Instruction *unknown_instruction = &_unknown_instruction;
const Instruction *instructions = _instructions;
const Instruction *cb_instructions = _cb_instructions;

// Returns the number of bytes following the instruction opcode for the
// operand.
static int operand_size(Operand operand) {
  switch (operand) {
  case NONE:
  case A:
  case SP:
  case HL:
  case CMEM:
  case R16:
  case R16STK:
  case R16MEM:
  case COND:
  case R8:
  case TGT3:
  case BIT_INDEX:
  case R8_DST:
    return 0;
  case SP_PLUS_IMM8:
  case IMM8:
  case IMM8_OFFSET:
  case IMM8MEM:
    return 1;
  case IMM16:
  case IMM16MEM:
    return 2;
  }
}

int instruction_size(const Instruction *instr) {
  int size = 1;
  // If the instruction is in the cb_instructions bank,
  // then add a byte to account for the 0xCB prefix.
  if (instr >= _cb_instructions &&
      instr <
          _cb_instructions + sizeof(_cb_instructions) / sizeof(Instruction)) {
    size += 1;
  }
  size += operand_size(instr->operand1);
  size += operand_size(instr->operand2);
  return size;
}

static int operand_op_code_bits(const Operand operand) {
  switch (operand) {
  case NONE:
  case A:
  case SP:
  case HL:
  case CMEM:
  case SP_PLUS_IMM8:
  case IMM8:
  case IMM8_OFFSET:
  case IMM8MEM:
  case IMM16:
  case IMM16MEM:
    return 0;

  case R16:
  case R16STK:
  case R16MEM:
  case COND:
    return 2;

  case R8:
  case TGT3:
  case BIT_INDEX:
  case R8_DST:
    return 3;
  }
}

static uint8_t op_code_mask(const Instruction *instr) {
  // Only one of operand1 or operand2 will be non-zero.
  int bits = operand_op_code_bits(instr->operand1) +
             operand_op_code_bits(instr->operand2);
  switch (bits) {
  case 0:
    return 0xFF;
  case 2:
    return ~(0x3 << instr->shift);
  case 3:
    return ~(0x7 << instr->shift);
  case 6:
    return ~(0x3F << instr->shift);
  default:
    fail("impossible operand bits: %d", bits);
  }
  return 0; // unreachable
}

const Instruction *find_instruction(const Instruction *bank, uint8_t op_code) {
  const Instruction *instr = bank;
  while (instr->mnemonic != NULL) {
    if ((op_code & op_code_mask(instr)) == instr->op_code) {
      return instr;
    }
    instr++;
  }
  return unknown_instruction;
}

const char *reg8_name(Reg8 r) {
  static const char *r8_names[] = {"B", "C", "D", "E", "H", "L", "[HL]", "A"};
  return r8_names[r];
}

const char *reg16_name(Reg16 r) {
  static const char *r16_names[] = {"BC", "DE", "HL", "SP", "AF", "HL+", "HL-"};
  return r16_names[r];
}

const char *cond_name(Cond c) {
  static const char *cond_names[] = {"NZ", "Z", "NC", "C"};
  return cond_names[c];
}

uint8_t get_reg8(const Cpu *cpu, Reg8 r) {
  if (r == REG_HL_MEM) {
    fail("get_reg8 on REG_HL_MEM");
  }
  return cpu->registers[r];
}

void set_reg8(Cpu *cpu, Reg8 r, uint8_t x) {
  if (r == REG_HL_MEM) {
    fail("set_reg8 on REG_HL_MEM");
  }
  cpu->registers[r] = x;
}

uint16_t get_reg16(const Cpu *cpu, Reg16 r) {
  switch (r) {
  case REG_BC:
    return (uint16_t)get_reg8(cpu, REG_B) << 8 | get_reg8(cpu, REG_C);
  case REG_DE:
    return (uint16_t)get_reg8(cpu, REG_D) << 8 | get_reg8(cpu, REG_E);
  case REG_HL:
  case REG_HL_PLUS:
  case REG_HL_MINUS:
    return (uint16_t)get_reg8(cpu, REG_H) << 8 | get_reg8(cpu, REG_L);
  case REG_SP:
    return cpu->sp;
  default:
    fail("invalid argument to get_reg16: %d", r);
  }
  return 0; // unreachable
}

void set_reg16_low_high(Cpu *cpu, Reg16 r, uint8_t low, uint8_t high) {
  switch (r) {
  case REG_BC:
    set_reg8(cpu, REG_B, high);
    set_reg8(cpu, REG_C, low);
    break;
  case REG_DE:
    set_reg8(cpu, REG_D, high);
    set_reg8(cpu, REG_E, low);
    break;
  case REG_HL:
  case REG_HL_PLUS:
  case REG_HL_MINUS:
    set_reg8(cpu, REG_H, high);
    set_reg8(cpu, REG_L, low);
    break;
  case REG_SP:
    cpu->sp = (uint16_t)high << 8 | low;
    break;
  default:
    fail("invalid argument to set_reg16: %d", r);
  }
}

void set_reg16(Cpu *cpu, Reg16 r, uint16_t x) {
  set_reg16_low_high(cpu, r, x & 0xFF, x >> 8);
}

static int r8(int shift, const Mem mem, Addr addr) {
  return (mem[addr] >> shift) & 0x7;
}

static int tgt3(int shift, const Mem mem, Addr addr) {
  return ((mem[addr] >> shift) & 0x7) * 8;
}

static int bit_index(int shift, const Mem mem, Addr addr) {
  return (mem[addr] >> (shift + 3)) & 0x7;
}

static int r8_dst(int shift, const Mem mem, Addr addr) {
  return (mem[addr] >> (shift + 3)) & 0x7;
}

static int r16(int shift, const Mem mem, Addr addr) {
  return (mem[addr] >> shift) & 0x3;
}

static int cond(int shift, const Mem mem, Addr addr) {
  return (mem[addr] >> shift) & 0x3;
}

static uint8_t imm8(const Mem mem, Addr addr) { return mem[addr]; }

static int8_t imm8_offset(const Mem mem, Addr addr) { return mem[addr]; }

static uint16_t imm16(const Mem mem, Addr addr) {
  return (int)mem[addr + 1] << 8 | mem[addr];
}

static int snprint_operand(char *buf, int size, Operand operand, int shift,
                           const Mem mem, Addr addr) {
  switch (operand) {
  case NONE:
    if (size > 0) {
      buf[0] = '\0';
    }
    return 0;
  case A:
    return snprintf(buf, size, "A");
  case SP:
    return snprintf(buf, size, "SP");
  case HL:
    return snprintf(buf, size, "HL");
  case CMEM:
    return snprintf(buf, size, "[C]");
  case SP_PLUS_IMM8:
    return snprintf(buf, size, "SP+%d", mem[addr]);
  case R16:
    return snprintf(buf, size, "%s",
                    reg16_name(decode_reg16(shift, mem[addr])));
  case R16STK:
    return snprintf(buf, size, "%s",
                    reg16_name(decode_reg16stk(shift, mem[addr])));
  case R16MEM:
    return snprintf(buf, size, "[%s]",
                    reg16_name(decode_reg16mem(shift, mem[addr])));
  case R8:
    return snprintf(buf, size, "%s", reg8_name(decode_reg8(shift, mem[addr])));
  case COND:
    return snprintf(buf, size, "%s", cond_name(decode_cond(shift, mem[addr])));
  case TGT3:
    return snprintf(buf, size, "%d", decode_tgt3(shift, mem[addr]));
  case BIT_INDEX:
    return snprintf(buf, size, "%d", decode_bit_index(shift, mem[addr]));
  case R8_DST:
    return snprintf(buf, size, "%s",
                    reg8_name(decode_reg8_dst(shift, mem[addr])));
  case IMM8:
    return snprintf(buf, size, "%d ($%02x)", mem[addr], mem[addr]);
  case IMM8_OFFSET:
    return snprintf(buf, size, "%+d ($%04x)", mem[addr], addr + 1 + mem[addr]);
  case IMM8MEM:
    return snprintf(buf, size, "[$FF%02x]", mem[addr]);
  case IMM16:
    int x = (int)mem[addr + 1] << 8 | mem[addr];
    return snprintf(buf, size, "%d ($%04x)", x, x);
  case IMM16MEM:
    return snprintf(buf, size, "[$%04x]", (int)mem[addr + 1] << 8 | mem[addr]);
  }
}

bool immediate_operand(Operand operand) { return operand_size(operand) > 0; }

const Instruction *format_instruction(char *out, int size, const Mem mem,
                                      Addr addr) {
  const Instruction *bank = instructions;
  if (mem[addr] == 0x76) {
    // This would normally be LD [HL], [HL], but it is special-cased to be
    // HALT.
    snprintf(out, size, "HALT");
    return find_instruction(bank, mem[addr]);
  }

  if (mem[addr] == 0xCB) {
    addr++;
    bank = cb_instructions;
  }

  const Instruction *instr = find_instruction(bank, mem[addr]);
  if (instr->operand1 == NONE) {
    snprintf(out, size, "%s", instr->mnemonic);
    return instr;
  }

  if (immediate_operand(instr->operand1)) {
    addr++;
  }
  char buf1[16];
  snprint_operand(buf1, sizeof(buf1), instr->operand1, instr->shift, mem, addr);
  if (instr->operand2 == NONE) {
    snprintf(out, size, "%s %s", instr->mnemonic, buf1);
    return instr;
  }

  if (immediate_operand(instr->operand2)) {
    addr++;
  }
  char buf2[16];
  snprint_operand(buf2, sizeof(buf2), instr->operand2, instr->shift, mem, addr);
  snprintf(out, size, "%s %s, %s", instr->mnemonic, buf1, buf2);
  return instr;
}
