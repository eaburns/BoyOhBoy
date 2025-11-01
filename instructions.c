#include "gameboy.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

struct instruction_tmpl {
  // The instruction mnemonic. For example "LD".
  const char *mnemonic;

  // If cb_prefix is true, this is a 2-byte op code.
  // The first byte is 0xCB, and the following byte
  // contains op_code as normal.
  bool cb_prefix;

  // The first byte of the instruction
  // (2nd byte in the case of cb_prefix==true), but with 0
  // in the place of any operands encoded into the first byte.
  uint8_t op_code;

  // Instructions can have 0, 1, or 2 operands.
  // If the instruction has more than one operand,
  // one of the operands is always an immediate value
  // that follows the first byte of the instruction.
  enum operand operand1, operand2;

  // If one of the operands is encoded into the 1st byte of the instruction,
  // this indicates the number of bits to right-shift to find the operand.
  int shift;

  // The function that executes this instruction.
  // Returns the number of cycles.
  int (*exec)(struct instruction *, struct gameboy *);
};

int operand_size(enum operand operand) {
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

int tmpl_size(const struct instruction_tmpl *tmpl) {
  int size = 1;
  if (tmpl->cb_prefix) {
    size += 1;
  }
  size += operand_size(tmpl->operand1);
  size += operand_size(tmpl->operand2);
  return size;
}

static uint8_t imm8(const uint8_t *data) { return data[1]; }

static int8_t imm8_offset(const uint8_t *data) { return data[1]; }

static uint16_t imm16(const uint8_t *data) {
  return (int)data[2] << 8 | data[1];
}

static const char *r8_names[] = {"B", "C", "D", "E", "H", "L", "[HL]", "A"};
static const char *r16_names[] = {"BC", "DE", "HL", "SP"};
static const char *r16stk_names[] = {"BC", "DE", "HL", "AF"};
static const char *r16mem_names[] = {"BC", "DE", "HL+", "HL-"};
static const char *cond_names[] = {"NZ", "Z", "NC", "C"};

static int r8(int shift, const uint8_t *data) {
  return (data[0] >> shift) & 0x7;
}

static int tgt3(int shift, const uint8_t *data) {
  return (data[0] >> shift) & 0x7;
}

static int bit_index(int shift, const uint8_t *data) {
  return (data[0] >> (shift + 3)) & 0x7;
}

static int r8_dst(int shift, const uint8_t *data) {
  return (data[0] >> (shift + 3)) & 0x7;
}

static int r16(int shift, const uint8_t *data) {
  return (data[0] >> shift) & 0x3;
}

static int cond(int shift, const uint8_t *data) {
  return (data[0] >> shift) & 0x3;
}

static void snprint_operand(char *buf, int size, enum operand operand,
                            int shift, const uint8_t *data) {
  if (size <= 0) {
    fail("snprint_operand with non-positive size: %d", size);
  }
  switch (operand) {
  case NONE:
    buf[0] = '\0';
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
    snprintf(buf, size, "SP+%d", data[1]);
    break;
  case R16:
    snprintf(buf, size, "%s", r16_names[r16(shift, data)]);
    break;
  case R16STK:
    snprintf(buf, size, "%s", r16stk_names[r16(shift, data)]);
    break;
  case R16MEM:
    snprintf(buf, size, "[%s]", r16mem_names[r16(shift, data)]);
    break;
  case R8:
    snprintf(buf, size, "%s", r8_names[r8(shift, data)]);
    break;
  case COND:
    snprintf(buf, size, "%s", cond_names[cond(shift, data)]);
    break;
  case TGT3:
    snprintf(buf, size, "%d", tgt3(shift, data));
    break;
  case BIT_INDEX:
    snprintf(buf, size, "%d", bit_index(shift, data));
    break;
  case R8_DST:
    snprintf(buf, size, "%s", r8_names[r8_dst(shift, data)]);
    break;
  case IMM8:
    snprintf(buf, size, "%d (0x%02x)", imm8(data), imm8(data));
    break;
  case IMM8_OFFSET:
    snprintf(buf, size, "%d", imm8_offset(data));
    break;
  case IMM8MEM:
    snprintf(buf, size, "[FF%02x]", imm8(data));
    break;
  case IMM16:
    snprintf(buf, size, "%d (0x%04x)", imm16(data), imm16(data));
    break;
  case IMM16MEM:
    snprintf(buf, size, "[0x%04x]", imm16(data));
    break;
  }
}

void snprint_instruction(char *out, int size, const struct instruction *instr) {
  if (instr->template == NULL) {
    snprintf(out, size, "UNKNOWN(%02x)", instr->data[0]);
    return;
  }
  const char *mnemonic = instr->template->mnemonic;
  int shift = instr->template->shift;
  enum operand operand1 = instr->template->operand1;
  enum operand operand2 = instr->template->operand2;
  if (operand1 == NONE) {
    snprintf(out, size, "%s", mnemonic);
    return;
  }
  if (operand2 == NONE) {
    char buf[16];
    snprint_operand(buf, sizeof(buf), operand1, shift, instr->data);
    snprintf(out, size, "%s %s", mnemonic, buf);
    return;
  }
  char buf1[16];
  snprint_operand(buf1, sizeof(buf1), operand1, shift, instr->data);
  char buf2[16];
  snprint_operand(buf2, sizeof(buf2), operand2, shift, instr->data);
  snprintf(out, size, "%s %s, %s", mnemonic, buf1, buf2);
}

int exec_nop(struct instruction *, struct gameboy *) { return -1; }
int exec_ld_r16_imm16(struct instruction *, struct gameboy *) { return -1; }
int exec_ld_r16mem_a(struct instruction *, struct gameboy *) { return -1; }
int exec_ld_a_r16mem(struct instruction *, struct gameboy *) { return -1; }
int exec_ld_imm16mem_sp(struct instruction *, struct gameboy *) { return -1; }
int exec_inc_r16(struct instruction *, struct gameboy *) { return -1; }
int exec_dec_r16(struct instruction *, struct gameboy *) { return -1; }
int exec_add_hl_r16(struct instruction *, struct gameboy *) { return -1; }
int exec_inc_r8(struct instruction *, struct gameboy *) { return -1; }
int exec_dec_r8(struct instruction *, struct gameboy *) { return -1; }
int exec_ld_r8_imm8(struct instruction *, struct gameboy *) { return -1; }
int exec_rlca(struct instruction *, struct gameboy *) { return -1; }
int exec_rrca(struct instruction *, struct gameboy *) { return -1; }
int exec_rla(struct instruction *, struct gameboy *) { return -1; }
int exec_rra(struct instruction *, struct gameboy *) { return -1; }
int exec_daa(struct instruction *, struct gameboy *) { return -1; }
int exec_cpl(struct instruction *, struct gameboy *) { return -1; }
int exec_scf(struct instruction *, struct gameboy *) { return -1; }
int exec_ccf(struct instruction *, struct gameboy *) { return -1; }
int exec_rlc_r8(struct instruction *, struct gameboy *) { return -1; }
int exec_rrc_r8(struct instruction *, struct gameboy *) { return -1; }
int exec_rl_r8(struct instruction *, struct gameboy *) { return -1; }
int exec_rr_r8(struct instruction *, struct gameboy *) { return -1; }
int exec_sla_r8(struct instruction *, struct gameboy *) { return -1; }
int exec_sra_r8(struct instruction *, struct gameboy *) { return -1; }
int exec_swap_r8(struct instruction *, struct gameboy *) { return -1; }
int exec_srl_r8(struct instruction *, struct gameboy *) { return -1; }
int exec_bit_b3_r8(struct instruction *, struct gameboy *) { return -1; }
int exec_res_b3_r8(struct instruction *, struct gameboy *) { return -1; }
int exec_set_b3_r8(struct instruction *, struct gameboy *) { return -1; }
int exec_jr_imm8(struct instruction *, struct gameboy *) { return -1; }
int exec_jr_cond_imm8(struct instruction *, struct gameboy *) { return -1; }
int exec_stop(struct instruction *, struct gameboy *) { return -1; }
int exec_ld_r8_r8(struct instruction *, struct gameboy *) { return -1; }
int exec_halt(struct instruction *, struct gameboy *) { return -1; }
int exec_add_a_r8(struct instruction *, struct gameboy *) { return -1; }
int exec_adc_a_r8(struct instruction *, struct gameboy *) { return -1; }
int exec_sub_a_r8(struct instruction *, struct gameboy *) { return -1; }
int exec_sbc_a_r8(struct instruction *, struct gameboy *) { return -1; }
int exec_and_a_r8(struct instruction *, struct gameboy *) { return -1; }
int exec_xor_a_r8(struct instruction *, struct gameboy *) { return -1; }
int exec_or_a_r8(struct instruction *, struct gameboy *) { return -1; }
int exec_cp_a_r8(struct instruction *, struct gameboy *) { return -1; }
int exec_add_a_imm8(struct instruction *, struct gameboy *) { return -1; }
int exec_adc_a_imm8(struct instruction *, struct gameboy *) { return -1; }
int exec_sub_a_imm8(struct instruction *, struct gameboy *) { return -1; }
int exec_sbc_a_imm8(struct instruction *, struct gameboy *) { return -1; }
int exec_and_a_imm8(struct instruction *, struct gameboy *) { return -1; }
int exec_xor_a_imm8(struct instruction *, struct gameboy *) { return -1; }
int exec_or_a_imm8(struct instruction *, struct gameboy *) { return -1; }
int exec_cp_a_imm8(struct instruction *, struct gameboy *) { return -1; }
int exec_ret_cond(struct instruction *, struct gameboy *) { return -1; }
int exec_ret(struct instruction *, struct gameboy *) { return -1; }
int exec_reti(struct instruction *, struct gameboy *) { return -1; }
int exec_jp_cond_imm16(struct instruction *, struct gameboy *) { return -1; }
int exec_jp_imm16(struct instruction *, struct gameboy *) { return -1; }
int exec_jp_hl(struct instruction *, struct gameboy *) { return -1; }
int exec_call_cond_imm16(struct instruction *, struct gameboy *) { return -1; }
int exec_call_imm16(struct instruction *, struct gameboy *) { return -1; }
int exec_rst_tgt3(struct instruction *, struct gameboy *) { return -1; }
int exec_pop_r16(struct instruction *, struct gameboy *) { return -1; }
int exec_push_r16(struct instruction *, struct gameboy *) { return -1; }
int exec_ldh_cmem_a(struct instruction *, struct gameboy *) { return -1; }
int exec_ldh_imm8mem_a(struct instruction *, struct gameboy *) { return -1; }
int exec_ld_imm16mem_a(struct instruction *, struct gameboy *) { return -1; }
int exec_ldh_a_cmem(struct instruction *, struct gameboy *) { return -1; }
int exec_ldh_a_imm8mem(struct instruction *, struct gameboy *) { return -1; }
int exec_ld_a_imm16mem(struct instruction *, struct gameboy *) { return -1; }
int exec_add_sp_imm8(struct instruction *, struct gameboy *) { return -1; }
int exec_ld_hl_sp_plus_imm8(struct instruction *, struct gameboy *) {
  return -1;
}
int exec_ld_sp_hl(struct instruction *, struct gameboy *) { return -1; }
int exec_di(struct instruction *, struct gameboy *) { return -1; }
int exec_ei(struct instruction *, struct gameboy *) { return -1; }

const struct instruction_tmpl instruction_templates[] = {
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
        .mnemonic = "RLC",
        .cb_prefix = true,
        .op_code = 0x00,
        .operand1 = R8,
        .shift = 0,
        .exec = exec_rlc_r8,
    },
    {
        .mnemonic = "RRC",
        .cb_prefix = true,
        .op_code = 0x08,
        .operand1 = R8,
        .shift = 0,
        .exec = exec_rrc_r8,
    },
    {
        .mnemonic = "RL",
        .cb_prefix = true,
        .op_code = 0x10,
        .operand1 = R8,
        .shift = 0,
        .exec = exec_rl_r8,
    },
    {
        .mnemonic = "RR",
        .cb_prefix = true,
        .op_code = 0x18,
        .operand1 = R8,
        .shift = 0,
        .exec = exec_rr_r8,
    },
    {
        .mnemonic = "SLA",
        .cb_prefix = true,
        .op_code = 0x20,
        .operand1 = R8,
        .shift = 0,
        .exec = exec_sla_r8,
    },
    {
        .mnemonic = "SRA",
        .cb_prefix = true,
        .op_code = 0x28,
        .operand1 = R8,
        .shift = 0,
        .exec = exec_sra_r8,
    },
    {
        .mnemonic = "SWAP",
        .cb_prefix = true,
        .op_code = 0x30,
        .operand1 = R8,
        .shift = 0,
        .exec = exec_swap_r8,
    },
    {
        .mnemonic = "SRL",
        .cb_prefix = true,
        .op_code = 0x38,
        .operand1 = R8,
        .shift = 0,
        .exec = exec_srl_r8,
    },
    {
        .mnemonic = "BIT",
        .cb_prefix = true,
        .op_code = 0x40,
        .operand1 = BIT_INDEX,
        .operand2 = R8,
        .shift = 0,
        .exec = exec_bit_b3_r8,
    },
    {
        .mnemonic = "RES",
        .cb_prefix = true,
        .op_code = 0x80,
        .operand1 = BIT_INDEX,
        .operand2 = R8,
        .shift = 0,
        .exec = exec_res_b3_r8,
    },
    {
        .mnemonic = "SET",
        .cb_prefix = true,
        .op_code = 0xB0,
        .operand1 = BIT_INDEX,
        .operand2 = R8,
        .shift = 0,
        .exec = exec_set_b3_r8,
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

static int operand_op_code_bits(const enum operand operand) {
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

static uint8_t op_code_mask(const struct instruction_tmpl *tmpl) {
  // Only one of operand1 or operand2 will be non-zero.
  int bits = operand_op_code_bits(tmpl->operand1) +
             operand_op_code_bits(tmpl->operand2);
  switch (bits) {
  case 0:
    return 0xFF;
  case 2:
    return ~(0x3 << tmpl->shift);
  case 3:
    return ~(0x7 << tmpl->shift);
  case 6:
    return ~(0x3F << tmpl->shift);
  default:
    fail("impossible operand bits: %d", bits);
  }
  return 0; // unreachable
}

struct instruction decode(const uint8_t *data, int size) {
  if (size <= 0) {
    fail("invalid object code size: %d", size);
  }
  const struct instruction_tmpl *tmpl = &instruction_templates[0];
  while (tmpl->mnemonic != NULL) {
    uint8_t mask = op_code_mask(tmpl);
    if (!tmpl->cb_prefix && (data[0] & mask) == tmpl->op_code ||
        tmpl->cb_prefix && data[0] == 0xCB &&
            (data[1] & mask) == tmpl->op_code) {
      break;
    }
    tmpl++;
  }

  if (tmpl->mnemonic == NULL) {
    // An unknown instruction. Just consume one byte.
    struct instruction instr = {
        .template = NULL,
        .size = 1,
        .data = {data[0], 0, 0},
    };
    // The only unknown instructions are
    // 0xD3, 0xDB, 0xDD, 0xE3, 0xE4, 0xEB, 0xEC, 0xED, 0xF4, 0xFC, and 0xFD
    if (data[0] != 0xD3 && data[0] != 0xDB && data[0] != 0xDD &&
        data[0] != 0xE3 && data[0] != 0xE4 && data[0] != 0xEB &&
        data[0] != 0xEC && data[0] != 0xED && data[0] != 0xF4 &&
        data[0] != 0xFC & data[0] != 0xFD) {
      fail("instruction 0x%02x should not be unknown", data[0]);
    }
    return instr;
  }
  struct instruction instr = {
      .template = tmpl,
      .size = tmpl_size(tmpl),
  };
  memcpy(&instr.data[0], data, instr.size);
  return instr;
}