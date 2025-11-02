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
  bool (*exec)(Gameboy *, Instruction *, int cycle);
};

static int r8(int shift, const Mem mem, Addr addr) {
  return (mem[addr] >> shift) & 0x7;
}

static int tgt3(int shift, const Mem mem, Addr addr) {
  return (mem[addr] >> shift) & 0x7;
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

static bool exec_nop(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_ld_r16_imm16(Gameboy *, Instruction *, int cycle) {
  return false;
}

static bool exec_ld_r16mem_a(Gameboy *, Instruction *, int cycle) {
  return false;
}

static bool exec_ld_a_r16mem(Gameboy *, Instruction *, int cycle) {
  return false;
}

static bool exec_ld_imm16mem_sp(Gameboy *, Instruction *, int cycle) {
  return false;
}

static bool exec_inc_r16(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_dec_r16(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_add_hl_r16(Gameboy *, Instruction *, int cycle) {
  return false;
}

static bool exec_inc_r8(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_dec_r8(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_ld_r8_imm8(Gameboy *, Instruction *, int cycle) {
  return false;
}

static bool exec_rlca(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_rrca(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_rla(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_rra(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_daa(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_cpl(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_scf(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_ccf(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_rlc_r8(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_rrc_r8(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_rl_r8(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_rr_r8(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_sla_r8(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_sra_r8(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_swap_r8(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_srl_r8(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_bit_b3_r8(Gameboy *, Instruction *, int cycle) {
  return false;
}

static bool exec_res_b3_r8(Gameboy *, Instruction *, int cycle) {
  return false;
}

static bool exec_set_b3_r8(Gameboy *, Instruction *, int cycle) {
  return false;
}

static bool exec_jr_imm8(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_jr_cond_imm8(Gameboy *, Instruction *, int cycle) {
  return false;
}

static bool exec_stop(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_ld_r8_r8(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_halt(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_add_a_r8(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_adc_a_r8(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_sub_a_r8(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_sbc_a_r8(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_and_a_r8(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_xor_a_r8(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_or_a_r8(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_cp_a_r8(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_add_a_imm8(Gameboy *, Instruction *, int cycle) {
  return false;
}

static bool exec_adc_a_imm8(Gameboy *, Instruction *, int cycle) {
  return false;
}

static bool exec_sub_a_imm8(Gameboy *, Instruction *, int cycle) {
  return false;
}

static bool exec_sbc_a_imm8(Gameboy *, Instruction *, int cycle) {
  return false;
}

static bool exec_and_a_imm8(Gameboy *, Instruction *, int cycle) {
  return false;
}

static bool exec_xor_a_imm8(Gameboy *, Instruction *, int cycle) {
  return false;
}

static bool exec_or_a_imm8(Gameboy *, Instruction *, int cycle) {
  return false;
}

static bool exec_cp_a_imm8(Gameboy *, Instruction *, int cycle) {
  return false;
}

static bool exec_ret_cond(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_ret(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_reti(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_jp_cond_imm16(Gameboy *, Instruction *, int cycle) {
  return false;
}

static bool exec_jp_imm16(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_jp_hl(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_call_cond_imm16(Gameboy *, Instruction *, int cycle) {
  return false;
}

static bool exec_call_imm16(Gameboy *, Instruction *, int cycle) {
  return false;
}

static bool exec_rst_tgt3(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_pop_r16(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_push_r16(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_ldh_cmem_a(Gameboy *, Instruction *, int cycle) {
  return false;
}

static bool exec_ldh_imm8mem_a(Gameboy *, Instruction *, int cycle) {
  return false;
}

static bool exec_ld_imm16mem_a(Gameboy *, Instruction *, int cycle) {
  return false;
}

static bool exec_ldh_a_cmem(Gameboy *, Instruction *, int cycle) {
  return false;
}

static bool exec_ldh_a_imm8mem(Gameboy *, Instruction *, int cycle) {
  return false;
}

static bool exec_ld_a_imm16mem(Gameboy *, Instruction *, int cycle) {
  return false;
}

static bool exec_add_sp_imm8(Gameboy *, Instruction *, int cycle) {
  return false;
}

static bool exec_ld_hl_sp_plus_imm8(Gameboy *, Instruction *, int cycle) {
  return false;
}

static bool exec_ld_sp_hl(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_di(Gameboy *, Instruction *, int cycle) { return false; }

static bool exec_ei(Gameboy *, Instruction *, int cycle) { return false; }

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
        .op_code = 0x1F,
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
        .op_code = 0xB0,
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

// Returns the number of bytes following the instruction opcode for the operand.
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

int instr_size(const Instruction *instr) {
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

static const char *r8_names[] = {"B", "C", "D", "E", "H", "L", "[HL]", "A"};
static const char *r16_names[] = {"BC", "DE", "HL", "SP"};
static const char *r16stk_names[] = {"BC", "DE", "HL", "AF"};
static const char *r16mem_names[] = {"BC", "DE", "HL+", "HL-"};
static const char *cond_names[] = {"NZ", "Z", "NC", "C"};

static void operand_snprint(char *buf, int size, Operand operand, int shift,
                            const Mem mem, Addr addr) {
  switch (operand) {
  case NONE:
    if (size > 0) {
      buf[0] = '\0';
    }
    break;
  case A:
    snprintf(buf, size, "A");
    break;
  case SP:
    snprintf(buf, size, "SP");
    break;
  case HL:
    snprintf(buf, size, "HL");
    break;
  case CMEM:
    snprintf(buf, size, "[C]");
    break;
  case SP_PLUS_IMM8:
    snprintf(buf, size, "SP+%d", mem[addr]);
    break;
  case R16:
    snprintf(buf, size, "%s", r16_names[r16(shift, mem, addr)]);
    break;
  case R16STK:
    snprintf(buf, size, "%s", r16stk_names[r16(shift, mem, addr)]);
    break;
  case R16MEM:
    snprintf(buf, size, "[%s]", r16mem_names[r16(shift, mem, addr)]);
    break;
  case R8:
    snprintf(buf, size, "%s", r8_names[r8(shift, mem, addr)]);
    break;
  case COND:
    snprintf(buf, size, "%s", cond_names[cond(shift, mem, addr)]);
    break;
  case TGT3:
    snprintf(buf, size, "%d", tgt3(shift, mem, addr));
    break;
  case BIT_INDEX:
    snprintf(buf, size, "%d", bit_index(shift, mem, addr));
    break;
  case R8_DST:
    snprintf(buf, size, "%s", r8_names[r8_dst(shift, mem, addr)]);
    break;
  case IMM8:
    snprintf(buf, size, "%d ($%02x)", imm8(mem, addr), imm8(mem, addr));
    break;
  case IMM8_OFFSET:
    snprintf(buf, size, "%+d ($%04x)", imm8_offset(mem, addr),
             addr + 1 + imm8_offset(mem, addr));
    break;
  case IMM8MEM:
    snprintf(buf, size, "[FF%02x]", imm8(mem, addr));
    break;
  case IMM16:
    snprintf(buf, size, "%d ($%04x)", imm16(mem, addr), imm16(mem, addr));
    break;
  case IMM16MEM:
    snprintf(buf, size, "[$%04x]", imm16(mem, addr));
    break;
  }
}

const Instruction *instr_snprint(char *out, int size, const Mem mem,
                                 Addr addr) {
  const Instruction *instr =
      mem[addr] == 0xCB ? find_instruction(cb_instructions, mem[addr + 1])
                        : find_instruction(instructions, mem[addr]);
  if (instr->operand1 == NONE) {
    snprintf(out, size, "%s", instr->mnemonic);
    return instr;
  }
  if (instr->operand2 == NONE) {
    char buf[16];
    operand_snprint(buf, sizeof(buf), instr->operand1, instr->shift, mem,
                    addr + 1);
    snprintf(out, size, "%s %s", instr->mnemonic, buf);
    return instr;
  }
  char buf1[16];
  operand_snprint(buf1, sizeof(buf1), instr->operand1, instr->shift, mem,
                  addr + 1);
  char buf2[16];
  operand_snprint(buf2, sizeof(buf2), instr->operand2, instr->shift, mem,
                  addr + operand_size(instr->operand1));
  snprintf(out, size, "%s %s, %s", instr->mnemonic, buf1, buf2);
  return instr;
}
