#include "gameboy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Calls fail with the current function name prefixed to the format string.
#define FAIL(...)                                                              \
  do {                                                                         \
    fprintf(stderr, "%s: ", __func__);                                         \
    fail(__VA_ARGS__);                                                         \
  } while (0)

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

enum {
  // Memory addresses.
  HIGH_RAM_START = 0xFF80,
  HIGH_RAM_END = 0xFFFE,

  // Flag combinations.
  FLAGS_NHC = FLAG_N | FLAG_H | FLAG_C,
  FLAGS_NH = FLAG_N | FLAG_H,
  FLAGS_ZNH = FLAG_Z | FLAG_N | FLAG_H,
  FLAGS_ZNHC = FLAG_Z | FLAG_N | FLAG_H | FLAG_C,

  // Instructions.
  NOP = 0x0,
  INCA = 0x3C,
  HALT = 0x76,
  RST0 = 0xC7,
  RET = 0xC9,
  RETI = 0xD9,
  DI = 0xF3,
  EI = 0xFB,
  LD_A_IMM16_MEM = 0xFA,
  LD_IMM16_MEM_A = 0xEA,
};

static int step(Gameboy *g) {
  int cycles = 0;
  do {
    cycles++;
    if (cycles == 10) {
      fail("too many cycles");
    }
    cpu_mcycle(g);
  } while (g->cpu.state == EXECUTING || g->cpu.state == INTERRUPTING);
  return cycles;
}

struct disassemble_test {
  uint8_t op;
  const char *str;
};

// The test tests the opcode followed by bytes 0x01 and 0x02.
// If loaded as imm8, the value is 1.
// If loaded as imm16, the value is 513.
static struct disassemble_test disassemble_tests[] = {
    {.op = 0x00, .str = "NOP"},
    {.op = 0x01, .str = "LD BC, 513 ($0201)"},
    {.op = 0x02, .str = "LD [BC], A"},
    {.op = 0x03, .str = "INC BC"},
    {.op = 0x04, .str = "INC B"},
    {.op = 0x05, .str = "DEC B"},
    {.op = 0x06, .str = "LD B, 1 ($01)"},
    {.op = 0x07, .str = "RLCA"},
    {.op = 0x08, .str = "LD [$0201], SP"},
    {.op = 0x09, .str = "ADD HL, BC"},
    {.op = 0x0A, .str = "LD A, [BC]"},
    {.op = 0x0B, .str = "DEC BC"},
    {.op = 0x0C, .str = "INC C"},
    {.op = 0x0D, .str = "DEC C"},
    {.op = 0x0E, .str = "LD C, 1 ($01)"},
    {.op = 0x0F, .str = "RRCA"},
    {.op = 0x10, .str = "STOP 1 ($01)"},
    {.op = 0x11, .str = "LD DE, 513 ($0201)"},
    {.op = 0x12, .str = "LD [DE], A"},
    {.op = 0x13, .str = "INC DE"},
    {.op = 0x14, .str = "INC D"},
    {.op = 0x15, .str = "DEC D"},
    {.op = 0x16, .str = "LD D, 1 ($01)"},
    {.op = 0x17, .str = "RLA"},
    {.op = 0x18, .str = "JR +1 ($0003)"},
    {.op = 0x19, .str = "ADD HL, DE"},
    {.op = 0x1A, .str = "LD A, [DE]"},
    {.op = 0x1B, .str = "DEC DE"},
    {.op = 0x1C, .str = "INC E"},
    {.op = 0x1D, .str = "DEC E"},
    {.op = 0x1E, .str = "LD E, 1 ($01)"},
    {.op = 0x1F, .str = "RRA"},
    {.op = 0x20, .str = "JR NZ, +1 ($0003)"},
    {.op = 0x21, .str = "LD HL, 513 ($0201)"},
    {.op = 0x22, .str = "LD [HL+], A"},
    {.op = 0x23, .str = "INC HL"},
    {.op = 0x24, .str = "INC H"},
    {.op = 0x25, .str = "DEC H"},
    {.op = 0x26, .str = "LD H, 1 ($01)"},
    {.op = 0x27, .str = "DAA"},
    {.op = 0x28, .str = "JR Z, +1 ($0003)"},
    {.op = 0x29, .str = "ADD HL, HL"},
    {.op = 0x2A, .str = "LD A, [HL+]"},
    {.op = 0x2B, .str = "DEC HL"},
    {.op = 0x2C, .str = "INC L"},
    {.op = 0x2D, .str = "DEC L"},
    {.op = 0x2E, .str = "LD L, 1 ($01)"},
    {.op = 0x2F, .str = "CPL"},
    {.op = 0x30, .str = "JR NC, +1 ($0003)"},
    {.op = 0x31, .str = "LD SP, 513 ($0201)"},
    {.op = 0x32, .str = "LD [HL-], A"},
    {.op = 0x33, .str = "INC SP"},
    {.op = 0x34, .str = "INC [HL]"},
    {.op = 0x35, .str = "DEC [HL]"},
    {.op = 0x36, .str = "LD [HL], 1 ($01)"},
    {.op = 0x37, .str = "SCF"},
    {.op = 0x38, .str = "JR C, +1 ($0003)"},
    {.op = 0x39, .str = "ADD HL, SP"},
    {.op = 0x3A, .str = "LD A, [HL-]"},
    {.op = 0x3B, .str = "DEC SP"},
    {.op = 0x3C, .str = "INC A"},
    {.op = 0x3D, .str = "DEC A"},
    {.op = 0x3E, .str = "LD A, 1 ($01)"},
    {.op = 0x3F, .str = "CCF"},
    {.op = 0x40, .str = "LD B, B"},
    {.op = 0x41, .str = "LD B, C"},
    {.op = 0x42, .str = "LD B, D"},
    {.op = 0x43, .str = "LD B, E"},
    {.op = 0x44, .str = "LD B, H"},
    {.op = 0x45, .str = "LD B, L"},
    {.op = 0x46, .str = "LD B, [HL]"},
    {.op = 0x47, .str = "LD B, A"},
    {.op = 0x48, .str = "LD C, B"},
    {.op = 0x49, .str = "LD C, C"},
    {.op = 0x4A, .str = "LD C, D"},
    {.op = 0x4B, .str = "LD C, E"},
    {.op = 0x4C, .str = "LD C, H"},
    {.op = 0x4D, .str = "LD C, L"},
    {.op = 0x4E, .str = "LD C, [HL]"},
    {.op = 0x4F, .str = "LD C, A"},
    {.op = 0x50, .str = "LD D, B"},
    {.op = 0x51, .str = "LD D, C"},
    {.op = 0x52, .str = "LD D, D"},
    {.op = 0x53, .str = "LD D, E"},
    {.op = 0x54, .str = "LD D, H"},
    {.op = 0x55, .str = "LD D, L"},
    {.op = 0x56, .str = "LD D, [HL]"},
    {.op = 0x57, .str = "LD D, A"},
    {.op = 0x58, .str = "LD E, B"},
    {.op = 0x59, .str = "LD E, C"},
    {.op = 0x5A, .str = "LD E, D"},
    {.op = 0x5B, .str = "LD E, E"},
    {.op = 0x5C, .str = "LD E, H"},
    {.op = 0x5D, .str = "LD E, L"},
    {.op = 0x5E, .str = "LD E, [HL]"},
    {.op = 0x5F, .str = "LD E, A"},
    {.op = 0x60, .str = "LD H, B"},
    {.op = 0x61, .str = "LD H, C"},
    {.op = 0x62, .str = "LD H, D"},
    {.op = 0x63, .str = "LD H, E"},
    {.op = 0x64, .str = "LD H, H"},
    {.op = 0x65, .str = "LD H, L"},
    {.op = 0x66, .str = "LD H, [HL]"},
    {.op = 0x67, .str = "LD H, A"},
    {.op = 0x68, .str = "LD L, B"},
    {.op = 0x69, .str = "LD L, C"},
    {.op = 0x6A, .str = "LD L, D"},
    {.op = 0x6B, .str = "LD L, E"},
    {.op = 0x6C, .str = "LD L, H"},
    {.op = 0x6D, .str = "LD L, L"},
    {.op = 0x6E, .str = "LD L, [HL]"},
    {.op = 0x6F, .str = "LD L, A"},
    {.op = 0x70, .str = "LD [HL], B"},
    {.op = 0x71, .str = "LD [HL], C"},
    {.op = 0x72, .str = "LD [HL], D"},
    {.op = 0x73, .str = "LD [HL], E"},
    {.op = 0x74, .str = "LD [HL], H"},
    {.op = 0x75, .str = "LD [HL], L"},
    {.op = 0x76, .str = "HALT"},
    {.op = 0x77, .str = "LD [HL], A"},
    {.op = 0x78, .str = "LD A, B"},
    {.op = 0x79, .str = "LD A, C"},
    {.op = 0x7A, .str = "LD A, D"},
    {.op = 0x7B, .str = "LD A, E"},
    {.op = 0x7C, .str = "LD A, H"},
    {.op = 0x7D, .str = "LD A, L"},
    {.op = 0x7E, .str = "LD A, [HL]"},
    {.op = 0x7F, .str = "LD A, A"},
    {.op = 0x80, .str = "ADD A, B"},
    {.op = 0x81, .str = "ADD A, C"},
    {.op = 0x82, .str = "ADD A, D"},
    {.op = 0x83, .str = "ADD A, E"},
    {.op = 0x84, .str = "ADD A, H"},
    {.op = 0x85, .str = "ADD A, L"},
    {.op = 0x86, .str = "ADD A, [HL]"},
    {.op = 0x87, .str = "ADD A, A"},
    {.op = 0x88, .str = "ADC A, B"},
    {.op = 0x89, .str = "ADC A, C"},
    {.op = 0x8A, .str = "ADC A, D"},
    {.op = 0x8B, .str = "ADC A, E"},
    {.op = 0x8C, .str = "ADC A, H"},
    {.op = 0x8D, .str = "ADC A, L"},
    {.op = 0x8E, .str = "ADC A, [HL]"},
    {.op = 0x8F, .str = "ADC A, A"},
    {.op = 0x90, .str = "SUB A, B"},
    {.op = 0x91, .str = "SUB A, C"},
    {.op = 0x92, .str = "SUB A, D"},
    {.op = 0x93, .str = "SUB A, E"},
    {.op = 0x94, .str = "SUB A, H"},
    {.op = 0x95, .str = "SUB A, L"},
    {.op = 0x96, .str = "SUB A, [HL]"},
    {.op = 0x97, .str = "SUB A, A"},
    {.op = 0x98, .str = "SBC A, B"},
    {.op = 0x99, .str = "SBC A, C"},
    {.op = 0x9A, .str = "SBC A, D"},
    {.op = 0x9B, .str = "SBC A, E"},
    {.op = 0x9C, .str = "SBC A, H"},
    {.op = 0x9D, .str = "SBC A, L"},
    {.op = 0x9E, .str = "SBC A, [HL]"},
    {.op = 0x9F, .str = "SBC A, A"},
    {.op = 0xA0, .str = "AND A, B"},
    {.op = 0xA1, .str = "AND A, C"},
    {.op = 0xA2, .str = "AND A, D"},
    {.op = 0xA3, .str = "AND A, E"},
    {.op = 0xA4, .str = "AND A, H"},
    {.op = 0xA5, .str = "AND A, L"},
    {.op = 0xA6, .str = "AND A, [HL]"},
    {.op = 0xA7, .str = "AND A, A"},
    {.op = 0xA8, .str = "XOR A, B"},
    {.op = 0xA9, .str = "XOR A, C"},
    {.op = 0xAA, .str = "XOR A, D"},
    {.op = 0xAB, .str = "XOR A, E"},
    {.op = 0xAC, .str = "XOR A, H"},
    {.op = 0xAD, .str = "XOR A, L"},
    {.op = 0xAE, .str = "XOR A, [HL]"},
    {.op = 0xAF, .str = "XOR A, A"},
    {.op = 0xB0, .str = "OR A, B"},
    {.op = 0xB1, .str = "OR A, C"},
    {.op = 0xB2, .str = "OR A, D"},
    {.op = 0xB3, .str = "OR A, E"},
    {.op = 0xB4, .str = "OR A, H"},
    {.op = 0xB5, .str = "OR A, L"},
    {.op = 0xB6, .str = "OR A, [HL]"},
    {.op = 0xB7, .str = "OR A, A"},
    {.op = 0xB8, .str = "CP A, B"},
    {.op = 0xB9, .str = "CP A, C"},
    {.op = 0xBA, .str = "CP A, D"},
    {.op = 0xBB, .str = "CP A, E"},
    {.op = 0xBC, .str = "CP A, H"},
    {.op = 0xBD, .str = "CP A, L"},
    {.op = 0xBE, .str = "CP A, [HL]"},
    {.op = 0xBF, .str = "CP A, A"},
    {.op = 0xC0, .str = "RET NZ"},
    {.op = 0xC1, .str = "POP BC"},
    {.op = 0xC2, .str = "JP NZ, $0201"},
    {.op = 0xC3, .str = "JP $0201"},
    {.op = 0xC4, .str = "CALL NZ, $0201"},
    {.op = 0xC5, .str = "PUSH BC"},
    {.op = 0xC6, .str = "ADD A, 1 ($01)"},
    {.op = 0xC7, .str = "RST 0"},
    {.op = 0xC8, .str = "RET Z"},
    {.op = 0xC9, .str = "RET"},
    {.op = 0xCA, .str = "JP Z, $0201"},
    /* 0xCB 0x01 0x02 is CB-prefixed instructions RLC C. */
    {.op = 0xCB, .str = "RLC C"},
    {.op = 0xCC, .str = "CALL Z, $0201"},
    {.op = 0xCD, .str = "CALL $0201"},
    {.op = 0xCE, .str = "ADC A, 1 ($01)"},
    {.op = 0xCF, .str = "RST 8"},
    {.op = 0xD0, .str = "RET NC"},
    {.op = 0xD1, .str = "POP DE"},
    {.op = 0xD2, .str = "JP NC, $0201"},
    {.op = 0xD3, .str = "UNKNOWN"},
    {.op = 0xD4, .str = "CALL NC, $0201"},
    {.op = 0xD5, .str = "PUSH DE"},
    {.op = 0xD6, .str = "SUB A, 1 ($01)"},
    {.op = 0xD7, .str = "RST 16"},
    {.op = 0xD8, .str = "RET C"},
    {.op = 0xD9, .str = "RETI"},
    {.op = 0xDA, .str = "JP C, $0201"},
    {.op = 0xDB, .str = "UNKNOWN"},
    {.op = 0xDC, .str = "CALL C, $0201"},
    {.op = 0xDD, .str = "UNKNOWN"},
    {.op = 0xDE, .str = "SBC A, 1 ($01)"},
    {.op = 0xDF, .str = "RST 24"},
    {.op = 0xE0, .str = "LDH [$FF01 (SERIAL_DATA)], A"},
    {.op = 0xE1, .str = "POP HL"},
    {.op = 0xE2, .str = "LDH [C], A"},
    {.op = 0xE3, .str = "UNKNOWN"},
    {.op = 0xE4, .str = "UNKNOWN"},
    {.op = 0xE5, .str = "PUSH HL"},
    {.op = 0xE6, .str = "AND A, 1 ($01)"},
    {.op = 0xE7, .str = "RST 32"},
    {.op = 0xE8, .str = "ADD SP, 1 ($01)"},
    {.op = 0xE9, .str = "JP HL"},
    {.op = 0xEA, .str = "LD [$0201], A"},
    {.op = 0xEB, .str = "UNKNOWN"},
    {.op = 0xEC, .str = "UNKNOWN"},
    {.op = 0xED, .str = "UNKNOWN"},
    {.op = 0xEE, .str = "XOR A, 1 ($01)"},
    {.op = 0xEF, .str = "RST 40"},
    {.op = 0xF0, .str = "LDH A, [$FF01 (SERIAL_DATA)]"},
    {.op = 0xF1, .str = "POP AF"},
    {.op = 0xF2, .str = "LDH A, [C]"},
    {.op = 0xF3, .str = "DI"},
    {.op = 0xF4, .str = "UNKNOWN"},
    {.op = 0xF5, .str = "PUSH AF"},
    {.op = 0xF6, .str = "OR A, 1 ($01)"},
    {.op = 0xF7, .str = "RST 48"},
    {.op = 0xF8, .str = "LD HL, SP+1"},
    {.op = 0xF9, .str = "LD SP, HL"},
    {.op = 0xFA, .str = "LD A, [$0201]"},
    {.op = 0xFB, .str = "EI"},
    {.op = 0xFC, .str = "UNKNOWN"},
    {.op = 0xFD, .str = "UNKNOWN"},
    {.op = 0xFE, .str = "CP A, 1 ($01)"},
    {.op = 0xFF, .str = "RST 56"},
};

static struct disassemble_test cb_disassemble_tests[] = {
    {.op = 0x00, .str = "RLC B"},       {.op = 0x01, .str = "RLC C"},
    {.op = 0x02, .str = "RLC D"},       {.op = 0x03, .str = "RLC E"},
    {.op = 0x04, .str = "RLC H"},       {.op = 0x05, .str = "RLC L"},
    {.op = 0x06, .str = "RLC [HL]"},    {.op = 0x07, .str = "RLC A"},
    {.op = 0x08, .str = "RRC B"},       {.op = 0x09, .str = "RRC C"},
    {.op = 0x0A, .str = "RRC D"},       {.op = 0x0B, .str = "RRC E"},
    {.op = 0x0C, .str = "RRC H"},       {.op = 0x0D, .str = "RRC L"},
    {.op = 0x0E, .str = "RRC [HL]"},    {.op = 0x0F, .str = "RRC A"},
    {.op = 0x10, .str = "RL B"},        {.op = 0x11, .str = "RL C"},
    {.op = 0x12, .str = "RL D"},        {.op = 0x13, .str = "RL E"},
    {.op = 0x14, .str = "RL H"},        {.op = 0x15, .str = "RL L"},
    {.op = 0x16, .str = "RL [HL]"},     {.op = 0x17, .str = "RL A"},
    {.op = 0x18, .str = "RR B"},        {.op = 0x19, .str = "RR C"},
    {.op = 0x1A, .str = "RR D"},        {.op = 0x1B, .str = "RR E"},
    {.op = 0x1C, .str = "RR H"},        {.op = 0x1D, .str = "RR L"},
    {.op = 0x1E, .str = "RR [HL]"},     {.op = 0x1F, .str = "RR A"},
    {.op = 0x20, .str = "SLA B"},       {.op = 0x21, .str = "SLA C"},
    {.op = 0x22, .str = "SLA D"},       {.op = 0x23, .str = "SLA E"},
    {.op = 0x24, .str = "SLA H"},       {.op = 0x25, .str = "SLA L"},
    {.op = 0x26, .str = "SLA [HL]"},    {.op = 0x27, .str = "SLA A"},
    {.op = 0x28, .str = "SRA B"},       {.op = 0x29, .str = "SRA C"},
    {.op = 0x2A, .str = "SRA D"},       {.op = 0x2B, .str = "SRA E"},
    {.op = 0x2C, .str = "SRA H"},       {.op = 0x2D, .str = "SRA L"},
    {.op = 0x2E, .str = "SRA [HL]"},    {.op = 0x2F, .str = "SRA A"},
    {.op = 0x30, .str = "SWAP B"},      {.op = 0x31, .str = "SWAP C"},
    {.op = 0x32, .str = "SWAP D"},      {.op = 0x33, .str = "SWAP E"},
    {.op = 0x34, .str = "SWAP H"},      {.op = 0x35, .str = "SWAP L"},
    {.op = 0x36, .str = "SWAP [HL]"},   {.op = 0x37, .str = "SWAP A"},
    {.op = 0x38, .str = "SRL B"},       {.op = 0x39, .str = "SRL C"},
    {.op = 0x3A, .str = "SRL D"},       {.op = 0x3B, .str = "SRL E"},
    {.op = 0x3C, .str = "SRL H"},       {.op = 0x3D, .str = "SRL L"},
    {.op = 0x3E, .str = "SRL [HL]"},    {.op = 0x3F, .str = "SRL A"},
    {.op = 0x40, .str = "BIT 0, B"},    {.op = 0x41, .str = "BIT 0, C"},
    {.op = 0x42, .str = "BIT 0, D"},    {.op = 0x43, .str = "BIT 0, E"},
    {.op = 0x44, .str = "BIT 0, H"},    {.op = 0x45, .str = "BIT 0, L"},
    {.op = 0x46, .str = "BIT 0, [HL]"}, {.op = 0x47, .str = "BIT 0, A"},
    {.op = 0x48, .str = "BIT 1, B"},    {.op = 0x49, .str = "BIT 1, C"},
    {.op = 0x4A, .str = "BIT 1, D"},    {.op = 0x4B, .str = "BIT 1, E"},
    {.op = 0x4C, .str = "BIT 1, H"},    {.op = 0x4D, .str = "BIT 1, L"},
    {.op = 0x4E, .str = "BIT 1, [HL]"}, {.op = 0x4F, .str = "BIT 1, A"},
    {.op = 0x50, .str = "BIT 2, B"},    {.op = 0x51, .str = "BIT 2, C"},
    {.op = 0x52, .str = "BIT 2, D"},    {.op = 0x53, .str = "BIT 2, E"},
    {.op = 0x54, .str = "BIT 2, H"},    {.op = 0x55, .str = "BIT 2, L"},
    {.op = 0x56, .str = "BIT 2, [HL]"}, {.op = 0x57, .str = "BIT 2, A"},
    {.op = 0x58, .str = "BIT 3, B"},    {.op = 0x59, .str = "BIT 3, C"},
    {.op = 0x5A, .str = "BIT 3, D"},    {.op = 0x5B, .str = "BIT 3, E"},
    {.op = 0x5C, .str = "BIT 3, H"},    {.op = 0x5D, .str = "BIT 3, L"},
    {.op = 0x5E, .str = "BIT 3, [HL]"}, {.op = 0x5F, .str = "BIT 3, A"},
    {.op = 0x60, .str = "BIT 4, B"},    {.op = 0x61, .str = "BIT 4, C"},
    {.op = 0x62, .str = "BIT 4, D"},    {.op = 0x63, .str = "BIT 4, E"},
    {.op = 0x64, .str = "BIT 4, H"},    {.op = 0x65, .str = "BIT 4, L"},
    {.op = 0x66, .str = "BIT 4, [HL]"}, {.op = 0x67, .str = "BIT 4, A"},
    {.op = 0x68, .str = "BIT 5, B"},    {.op = 0x69, .str = "BIT 5, C"},
    {.op = 0x6A, .str = "BIT 5, D"},    {.op = 0x6B, .str = "BIT 5, E"},
    {.op = 0x6C, .str = "BIT 5, H"},    {.op = 0x6D, .str = "BIT 5, L"},
    {.op = 0x6E, .str = "BIT 5, [HL]"}, {.op = 0x6F, .str = "BIT 5, A"},
    {.op = 0x70, .str = "BIT 6, B"},    {.op = 0x71, .str = "BIT 6, C"},
    {.op = 0x72, .str = "BIT 6, D"},    {.op = 0x73, .str = "BIT 6, E"},
    {.op = 0x74, .str = "BIT 6, H"},    {.op = 0x75, .str = "BIT 6, L"},
    {.op = 0x76, .str = "BIT 6, [HL]"}, {.op = 0x77, .str = "BIT 6, A"},
    {.op = 0x78, .str = "BIT 7, B"},    {.op = 0x79, .str = "BIT 7, C"},
    {.op = 0x7A, .str = "BIT 7, D"},    {.op = 0x7B, .str = "BIT 7, E"},
    {.op = 0x7C, .str = "BIT 7, H"},    {.op = 0x7D, .str = "BIT 7, L"},
    {.op = 0x7E, .str = "BIT 7, [HL]"}, {.op = 0x7F, .str = "BIT 7, A"},
    {.op = 0x80, .str = "RES 0, B"},    {.op = 0x81, .str = "RES 0, C"},
    {.op = 0x82, .str = "RES 0, D"},    {.op = 0x83, .str = "RES 0, E"},
    {.op = 0x84, .str = "RES 0, H"},    {.op = 0x85, .str = "RES 0, L"},
    {.op = 0x86, .str = "RES 0, [HL]"}, {.op = 0x87, .str = "RES 0, A"},
    {.op = 0x88, .str = "RES 1, B"},    {.op = 0x89, .str = "RES 1, C"},
    {.op = 0x8A, .str = "RES 1, D"},    {.op = 0x8B, .str = "RES 1, E"},
    {.op = 0x8C, .str = "RES 1, H"},    {.op = 0x8D, .str = "RES 1, L"},
    {.op = 0x8E, .str = "RES 1, [HL]"}, {.op = 0x8F, .str = "RES 1, A"},
    {.op = 0x90, .str = "RES 2, B"},    {.op = 0x91, .str = "RES 2, C"},
    {.op = 0x92, .str = "RES 2, D"},    {.op = 0x93, .str = "RES 2, E"},
    {.op = 0x94, .str = "RES 2, H"},    {.op = 0x95, .str = "RES 2, L"},
    {.op = 0x96, .str = "RES 2, [HL]"}, {.op = 0x97, .str = "RES 2, A"},
    {.op = 0x98, .str = "RES 3, B"},    {.op = 0x99, .str = "RES 3, C"},
    {.op = 0x9A, .str = "RES 3, D"},    {.op = 0x9B, .str = "RES 3, E"},
    {.op = 0x9C, .str = "RES 3, H"},    {.op = 0x9D, .str = "RES 3, L"},
    {.op = 0x9E, .str = "RES 3, [HL]"}, {.op = 0x9F, .str = "RES 3, A"},
    {.op = 0xA0, .str = "RES 4, B"},    {.op = 0xA1, .str = "RES 4, C"},
    {.op = 0xA2, .str = "RES 4, D"},    {.op = 0xA3, .str = "RES 4, E"},
    {.op = 0xA4, .str = "RES 4, H"},    {.op = 0xA5, .str = "RES 4, L"},
    {.op = 0xA6, .str = "RES 4, [HL]"}, {.op = 0xA7, .str = "RES 4, A"},
    {.op = 0xA8, .str = "RES 5, B"},    {.op = 0xA9, .str = "RES 5, C"},
    {.op = 0xAA, .str = "RES 5, D"},    {.op = 0xAB, .str = "RES 5, E"},
    {.op = 0xAC, .str = "RES 5, H"},    {.op = 0xAD, .str = "RES 5, L"},
    {.op = 0xAE, .str = "RES 5, [HL]"}, {.op = 0xAF, .str = "RES 5, A"},
    {.op = 0xB0, .str = "RES 6, B"},    {.op = 0xB1, .str = "RES 6, C"},
    {.op = 0xB2, .str = "RES 6, D"},    {.op = 0xB3, .str = "RES 6, E"},
    {.op = 0xB4, .str = "RES 6, H"},    {.op = 0xB5, .str = "RES 6, L"},
    {.op = 0xB6, .str = "RES 6, [HL]"}, {.op = 0xB7, .str = "RES 6, A"},
    {.op = 0xB8, .str = "RES 7, B"},    {.op = 0xB9, .str = "RES 7, C"},
    {.op = 0xBA, .str = "RES 7, D"},    {.op = 0xBB, .str = "RES 7, E"},
    {.op = 0xBC, .str = "RES 7, H"},    {.op = 0xBD, .str = "RES 7, L"},
    {.op = 0xBE, .str = "RES 7, [HL]"}, {.op = 0xBF, .str = "RES 7, A"},
    {.op = 0xC0, .str = "SET 0, B"},    {.op = 0xC1, .str = "SET 0, C"},
    {.op = 0xC2, .str = "SET 0, D"},    {.op = 0xC3, .str = "SET 0, E"},
    {.op = 0xC4, .str = "SET 0, H"},    {.op = 0xC5, .str = "SET 0, L"},
    {.op = 0xC6, .str = "SET 0, [HL]"}, {.op = 0xC7, .str = "SET 0, A"},
    {.op = 0xC8, .str = "SET 1, B"},    {.op = 0xC9, .str = "SET 1, C"},
    {.op = 0xCA, .str = "SET 1, D"},    {.op = 0xCB, .str = "SET 1, E"},
    {.op = 0xCC, .str = "SET 1, H"},    {.op = 0xCD, .str = "SET 1, L"},
    {.op = 0xCE, .str = "SET 1, [HL]"}, {.op = 0xCF, .str = "SET 1, A"},
    {.op = 0xD0, .str = "SET 2, B"},    {.op = 0xD1, .str = "SET 2, C"},
    {.op = 0xD2, .str = "SET 2, D"},    {.op = 0xD3, .str = "SET 2, E"},
    {.op = 0xD4, .str = "SET 2, H"},    {.op = 0xD5, .str = "SET 2, L"},
    {.op = 0xD6, .str = "SET 2, [HL]"}, {.op = 0xD7, .str = "SET 2, A"},
    {.op = 0xD8, .str = "SET 3, B"},    {.op = 0xD9, .str = "SET 3, C"},
    {.op = 0xDA, .str = "SET 3, D"},    {.op = 0xDB, .str = "SET 3, E"},
    {.op = 0xDC, .str = "SET 3, H"},    {.op = 0xDD, .str = "SET 3, L"},
    {.op = 0xDE, .str = "SET 3, [HL]"}, {.op = 0xDF, .str = "SET 3, A"},
    {.op = 0xE0, .str = "SET 4, B"},    {.op = 0xE1, .str = "SET 4, C"},
    {.op = 0xE2, .str = "SET 4, D"},    {.op = 0xE3, .str = "SET 4, E"},
    {.op = 0xE4, .str = "SET 4, H"},    {.op = 0xE5, .str = "SET 4, L"},
    {.op = 0xE6, .str = "SET 4, [HL]"}, {.op = 0xE7, .str = "SET 4, A"},
    {.op = 0xE8, .str = "SET 5, B"},    {.op = 0xE9, .str = "SET 5, C"},
    {.op = 0xEA, .str = "SET 5, D"},    {.op = 0xEB, .str = "SET 5, E"},
    {.op = 0xEC, .str = "SET 5, H"},    {.op = 0xED, .str = "SET 5, L"},
    {.op = 0xEE, .str = "SET 5, [HL]"}, {.op = 0xEF, .str = "SET 5, A"},
    {.op = 0xF0, .str = "SET 6, B"},    {.op = 0xF1, .str = "SET 6, C"},
    {.op = 0xF2, .str = "SET 6, D"},    {.op = 0xF3, .str = "SET 6, E"},
    {.op = 0xF4, .str = "SET 6, H"},    {.op = 0xF5, .str = "SET 6, L"},
    {.op = 0xF6, .str = "SET 6, [HL]"}, {.op = 0xF7, .str = "SET 6, A"},
    {.op = 0xF8, .str = "SET 7, B"},    {.op = 0xF9, .str = "SET 7, C"},
    {.op = 0xFA, .str = "SET 7, D"},    {.op = 0xFB, .str = "SET 7, E"},
    {.op = 0xFC, .str = "SET 7, H"},    {.op = 0xFD, .str = "SET 7, L"},
    {.op = 0xFE, .str = "SET 7, [HL]"}, {.op = 0xFF, .str = "SET 7, A"},
};

void run_disassemble_tests() {
  for (struct disassemble_test *test = disassemble_tests;
       test < disassemble_tests + ARRAY_SIZE(disassemble_tests); test++) {
    Mem mem;
    mem[0] = test->op;
    mem[1] = 0x01;
    mem[2] = 0x02;
    Disasm disasm = disassemble(mem, MEM_SIZE, 0);
    if (strcmp(disasm.instr, test->str) != 0) {
      FAIL("op_code: 0x%02X printed as %s, but expected %s", test->op,
           disasm.instr, test->str);
    }
  }
}

void run_disassemble_zero_test() {
  Disasm disasm = disassemble(NULL, 0, 0);
  const char *full = "0000:         		UNKNOWN";
  if (strcmp(disasm.full, full) != 0) {
    FAIL("full: got [%s], wanted [%s]\n", disasm.full, full);
  }
  if (strcmp(disasm.instr, "UNKNOWN") != 0) {
    FAIL("instr: got [%s], wanted [UNKNOWN]\n", disasm.instr);
  }
  if (disasm.size != 0) {
    FAIL("got size %d, wanted 0", disasm.size);
  }
}

void run_disassemble_instr_too_big_mem_size_1_test() {
  uint8_t ld_bc_imm16[3] = {0x01, 0xFF, 0xAA};
  Disasm disasm = disassemble(ld_bc_imm16, 1, 0);
  const char *full = "0000: 01      		UNKNOWN";
  if (strcmp(disasm.full, full) != 0) {
    FAIL("full: got [%s], wanted [%s]\n", disasm.full, full);
  }
  if (strcmp(disasm.instr, "UNKNOWN") != 0) {
    FAIL("instr: got [%s], wanted [UNKNOWN]\n", disasm.instr);
  }
  if (disasm.size != 1) {
    FAIL("got size %d, wanted 1", disasm.size);
  }
}

void run_disassemble_instr_too_big_mem_size_2_test() {
  uint8_t ld_bc_imm16[3] = {0x01, 0xFF, 0xAA};
  Disasm disasm = disassemble(ld_bc_imm16, 2, 0);
  const char *full = "0000: 01      		UNKNOWN";
  if (strcmp(disasm.full, full) != 0) {
    FAIL("full: got [%s], wanted [%s]\n", disasm.full, full);
  }
  if (strcmp(disasm.instr, "UNKNOWN") != 0) {
    FAIL("instr: got [%s], wanted [UNKNOWN]\n", disasm.instr);
  }
  if (disasm.size != 1) {
    FAIL("got size %d, wanted 1", disasm.size);
  }
}

void run_disassemble_cb_instr_too_big_test() {
  uint8_t ld_bc_imm16[3] = {0xCB, 0xAA};
  Disasm disasm = disassemble(ld_bc_imm16, 1, 0);
  const char *full = "0000: CB      		UNKNOWN";
  if (strcmp(disasm.full, full) != 0) {
    FAIL("full: got [%s], wanted [%s]\n", disasm.full, full);
  }
  if (strcmp(disasm.instr, "UNKNOWN") != 0) {
    FAIL("instr: got [%s], wanted [UNKNOWN]\n", disasm.instr);
  }
  if (disasm.size != 1) {
    FAIL("got size %d, wanted 1", disasm.size);
  }
}

void run_disassemble_instr_too_big_mem_size_3_offs_1_test() {
  uint8_t ld_bc_imm16[4] = {0x00, 0x01, 0xFF, 0xAA};
  Disasm disasm = disassemble(ld_bc_imm16, 3, 1);
  const char *full = "0001: 01      		UNKNOWN";
  if (strcmp(disasm.full, full) != 0) {
    FAIL("full: got [%s], wanted [%s]\n", disasm.full, full);
  }
  if (strcmp(disasm.instr, "UNKNOWN") != 0) {
    FAIL("instr: got [%s], wanted [UNKNOWN]\n", disasm.instr);
  }
  if (disasm.size != 1) {
    FAIL("got size %d, wanted 1", disasm.size);
  }
}
void run_cb_disassemble_tests() {
  for (struct disassemble_test *test = cb_disassemble_tests;
       test < cb_disassemble_tests + ARRAY_SIZE(cb_disassemble_tests); test++) {
    Mem mem;
    mem[0] = 0xCB;
    mem[1] = test->op;
    mem[2] = 0x01;
    mem[3] = 0x02;
    Disasm disasm = disassemble(mem, MEM_SIZE, 0);
    if (strcmp(disasm.instr, test->str) != 0) {
      FAIL("op_code: 0x%02X printed as %s, but expected %s", test->op,
           disasm.instr, test->str);
    }
  }
}

void run_reg8_get_set_tests() {
  for (Reg8 r = REG_B; r <= REG_A; r++) {
    Cpu cpu = {};
    set_reg8(&cpu, r, 1);
    for (Reg8 s = REG_B; r <= REG_A; r++) {
      if (r == REG_HL_MEM) {
        continue;
      }
      uint8_t got = get_reg8(&cpu, r);
      if (s == r && got != 1) {
        FAIL("set_reg(%s, 1), get_reg(%s)=%d, wanted 1", reg8_name(r),
             reg8_name(r), got);
      }
      if (s != r && got != 0) {
        FAIL("set_reg(%s, 1), get_reg(%s)=%d, wanted 0", reg8_name(r),
             reg8_name(r), got);
      }
    }
  }
}

void run_reg16_get_set_tests() {
  {
    Cpu cpu = {};
    set_reg16_low_high(&cpu, REG_BC, 1, 2);
    if (get_reg16(&cpu, REG_BC) != 0x0201) {
      FAIL("set_reg(BC, 1), get_reg(BC)=0x%04X, wanted 0x0201",
           get_reg16(&cpu, REG_BC));
    }
    if (get_reg8(&cpu, REG_B) != 2) {
      FAIL("set_reg(BC, 1), get_reg(B)=%d, wanted 2", get_reg8(&cpu, REG_B));
    }
    if (get_reg8(&cpu, REG_C) != 1) {
      FAIL("set_reg(BC, 1), get_reg(C)=%d, wanted 1", get_reg8(&cpu, REG_C));
    }
    if (get_reg8(&cpu, REG_D) != 0) {
      FAIL("set_reg(BC, 1), get_reg(D)=%d, wanted 0", get_reg8(&cpu, REG_D));
    }
    if (get_reg8(&cpu, REG_E) != 0) {
      FAIL("set_reg(BC, 1), get_reg(E)=%d, wanted 0", get_reg8(&cpu, REG_E));
    }
    if (get_reg8(&cpu, REG_H) != 0) {
      FAIL("set_reg(BC, 1), get_reg(H)=%d, wanted 0", get_reg8(&cpu, REG_H));
    }
    if (get_reg8(&cpu, REG_L) != 0) {
      FAIL("set_reg(BC, 1), get_reg(L)=%d, wanted 0", get_reg8(&cpu, REG_L));
    }
    if (get_reg8(&cpu, REG_A) != 0) {
      FAIL("set_reg(BC, 1), get_reg(A)=%d, wanted 0", get_reg8(&cpu, REG_A));
    }
    if (cpu.sp != 0) {
      FAIL("set_reg(BC, 1), get_reg(SP)=%d, wanted 0", cpu.sp);
    }
  }
  {
    Cpu cpu = {};
    set_reg16_low_high(&cpu, REG_DE, 1, 2);
    if (get_reg16(&cpu, REG_DE) != 0x0201) {
      FAIL("set_reg(DE, 1), get_reg(DE)=0x%04X, wanted 0x0201",
           get_reg16(&cpu, REG_DE));
    }
    if (get_reg8(&cpu, REG_B) != 0) {
      FAIL("set_reg(DE, 1), get_reg(B)=%d, wanted 0", get_reg8(&cpu, REG_B));
    }
    if (get_reg8(&cpu, REG_C) != 0) {
      FAIL("set_reg(DE, 1), get_reg(C)=%d, wanted 0", get_reg8(&cpu, REG_C));
    }
    if (get_reg8(&cpu, REG_D) != 2) {
      FAIL("set_reg(DE, 1), get_reg(D)=%d, wanted 2", get_reg8(&cpu, REG_D));
    }
    if (get_reg8(&cpu, REG_E) != 1) {
      FAIL("set_reg(DE, 1), get_reg(E)=%d, wanted 1", get_reg8(&cpu, REG_E));
    }
    if (get_reg8(&cpu, REG_H) != 0) {
      FAIL("set_reg(DE, 1), get_reg(H)=%d, wanted 0", get_reg8(&cpu, REG_H));
    }
    if (get_reg8(&cpu, REG_L) != 0) {
      FAIL("set_reg(DE, 1), get_reg(L)=%d, wanted 0", get_reg8(&cpu, REG_L));
    }
    if (get_reg8(&cpu, REG_A) != 0) {
      FAIL("set_reg(DE, 1), get_reg(A)=%d, wanted 0", get_reg8(&cpu, REG_A));
    }
    if (cpu.sp != 0) {
      FAIL("set_reg(DE, 1), get_reg(SP)=%d, wanted 0", cpu.sp);
    }
  }
  {
    Cpu cpu = {};
    set_reg16_low_high(&cpu, REG_HL, 1, 2);
    if (get_reg16(&cpu, REG_HL) != 0x0201) {
      FAIL("set_reg(HL, 1), get_reg(HL)=0x%04X, wanted 0x0201",
           get_reg16(&cpu, REG_HL));
    }
    if (get_reg8(&cpu, REG_B) != 0) {
      FAIL("set_reg(HL, 1), get_reg(B)=%d, wanted 0", get_reg8(&cpu, REG_B));
    }
    if (get_reg8(&cpu, REG_C) != 0) {
      FAIL("set_reg(HL, 1), get_reg(C)=%d, wanted 0", get_reg8(&cpu, REG_C));
    }
    if (get_reg8(&cpu, REG_D) != 0) {
      FAIL("set_reg(HL, 1), get_reg(D)=%d, wanted 0", get_reg8(&cpu, REG_D));
    }
    if (get_reg8(&cpu, REG_E) != 0) {
      FAIL("set_reg(HL, 1), get_reg(E)=%d, wanted 0", get_reg8(&cpu, REG_E));
    }
    if (get_reg8(&cpu, REG_H) != 2) {
      FAIL("set_reg(HL, 1), get_reg(H)=%d, wanted 2", get_reg8(&cpu, REG_H));
    }
    if (get_reg8(&cpu, REG_L) != 1) {
      FAIL("set_reg(HL, 1), get_reg(L)=%d, wanted 1", get_reg8(&cpu, REG_L));
    }
    if (get_reg8(&cpu, REG_A) != 0) {
      FAIL("set_reg(HL, 1), get_reg(A)=%d, wanted 0", get_reg8(&cpu, REG_A));
    }
    if (cpu.sp != 0) {
      FAIL("set_reg(HL, 1), get_reg(SP)=%d, wanted 0", cpu.sp);
    }
  }
  {
    Cpu cpu = {};
    set_reg16_low_high(&cpu, REG_SP, 1, 2);
    if (get_reg16(&cpu, REG_SP) != 0x0201) {
      FAIL("set_reg(SP, 1), get_reg(SP)=0x%04X, wanted 0x0201",
           get_reg16(&cpu, REG_SP));
    }
    if (get_reg8(&cpu, REG_B) != 0) {
      FAIL("set_reg(SP, 1), get_reg(B)=%d, wanted 0", get_reg8(&cpu, REG_B));
    }
    if (get_reg8(&cpu, REG_C) != 0) {
      FAIL("set_reg(SP, 1), get_reg(C)=%d, wanted 0", get_reg8(&cpu, REG_C));
    }
    if (get_reg8(&cpu, REG_D) != 0) {
      FAIL("set_reg(SP, 1), get_reg(D)=%d, wanted 0", get_reg8(&cpu, REG_D));
    }
    if (get_reg8(&cpu, REG_E) != 0) {
      FAIL("set_reg(SP, 1), get_reg(E)=%d, wanted 0", get_reg8(&cpu, REG_E));
    }
    if (get_reg8(&cpu, REG_H) != 0) {
      FAIL("set_reg(SP, 1), get_reg(H)=%d, wanted 0", get_reg8(&cpu, REG_H));
    }
    if (get_reg8(&cpu, REG_L) != 0) {
      FAIL("set_reg(SP, 1), get_reg(L)=%d, wanted 0", get_reg8(&cpu, REG_L));
    }
    if (get_reg8(&cpu, REG_A) != 0) {
      FAIL("set_reg(SP, 1), get_reg(A)=%d, wanted 0", get_reg8(&cpu, REG_A));
    }
    if (cpu.sp != 0x0201) {
      FAIL("set_reg(SP, 1), get_reg(SP)=%d, wanted 0x0201", cpu.sp);
    }
  }

  // Test that set_reg16 is using the right byte order.
  {
    Cpu cpu = {};
    set_reg16(&cpu, REG_BC, 0x0102);
    if (get_reg16(&cpu, REG_BC) != 0x0102) {
      FAIL("set_reg(BC, 1), get_reg(BC)=0x%04X, wanted 0x0201",
           get_reg16(&cpu, REG_BC));
    }
  }
}

struct exec_test {
  const char *name;
  const Rom rom;
  const Gameboy init;
  const Gameboy want;
  int cycles;
};

static struct exec_test
    exec_tests[] =
        {
            {
                .name = "(exec_nop) NOP",
                .init =
                    {
                        .cpu = {.ir = 0x00},
                        .mem = {0x00, 0x01},
                    },
                .want = {.cpu = {.pc = 1, .ir = 0x00}, .mem = {0x00, 0x01}},
                .cycles = 1,
            },
            {
                .name = "(exec_ld_r16_imm16) LD BC, imm16",
                .init =
                    {
                        .cpu = {.ir = 0x01},
                        .mem = {0x01, 0x02, 0x03, 0x4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_B] = 0x02, [REG_C] = 0x01},
                                .pc = 3,
                                .ir = 0x03,
                            },
                        .mem = {0x01, 0x02, 0x03, 0x4},
                    },
                .cycles = 3,
            },
            {
                .name = "(exec_ld_r16mem_a) LD [BC], A",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x02,
                                .registers =
                                    {
                                        [REG_B] = HIGH_RAM_START >> 8,
                                        [REG_C] = HIGH_RAM_START & 0xFF,
                                        [REG_A] = 0x12,
                                    },
                            },
                        .mem = {0x01, 0x02, 0x03, 0x4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers =
                                    {
                                        [REG_B] = HIGH_RAM_START >> 8,
                                        [REG_C] = HIGH_RAM_START & 0xFF,
                                        [REG_A] = 0x12,
                                    },
                                .pc = 1,
                                .ir = 0x01,
                            },
                        .mem = {0x01, 0x02, 0x03, 0x4, [HIGH_RAM_START] = 0x12},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_ld_r16mem_a) LD [HL+], A",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x22,
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                        [REG_A] = 0x12,
                                    },
                            },
                        .mem = {0x01, 0x02, 0x03, 0x4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = (HIGH_RAM_START & 0xFF) + 1,
                                        [REG_A] = 0x12,
                                    },
                                .pc = 1,
                                .ir = 0x01,
                            },
                        .mem = {0x01, 0x02, 0x03, 0x4, [HIGH_RAM_START] = 0x12},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_ld_r16mem_a) LD [HL-], A",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x32,
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                        [REG_A] = 0x12,
                                    },
                            },
                        .mem = {0x01, 0x02, 0x03, 0x4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = (HIGH_RAM_START & 0xFF) - 1,
                                        [REG_A] = 0x12,
                                    },
                                .pc = 1,
                                .ir = 0x01,
                            },
                        .mem = {0x01, 0x02, 0x03, 0x4, [HIGH_RAM_START] = 0x12},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_ld_a_r16mem) LD A, [BC]",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x0A,
                                .registers =
                                    {
                                        [REG_B] = HIGH_RAM_START >> 8,
                                        [REG_C] = HIGH_RAM_START & 0xFF,
                                    },
                            },
                        .mem = {0x01, 0x02, 0x03, 0x4, [HIGH_RAM_START] = 0x12},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers =
                                    {
                                        [REG_B] = HIGH_RAM_START >> 8,
                                        [REG_C] = HIGH_RAM_START & 0xFF,
                                        [REG_A] = 0x12,
                                    },
                                .pc = 1,
                                .ir = 0x01,
                            },
                        .mem = {0x01, 0x02, 0x03, 0x4, [HIGH_RAM_START] = 0x12},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_ld_a_r16mem) LD [HL+], A",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x2A,
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                            },
                        .mem = {0x01, 0x02, 0x03, 0x4, [HIGH_RAM_START] = 0x12},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = (HIGH_RAM_START & 0xFF) + 1,
                                        [REG_A] = 0x12,
                                    },
                                .pc = 1,
                                .ir = 0x01,
                            },
                        .mem = {0x01, 0x02, 0x03, 0x4, [HIGH_RAM_START] = 0x12},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_ld_a_r16mem) LD A, [HL-]",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x3A,
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                            },
                        .mem = {0x01, 0x02, 0x03, 0x4, [HIGH_RAM_START] = 0x12},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = (HIGH_RAM_START & 0xFF) - 1,
                                        [REG_A] = 0x12,
                                    },
                                .pc = 1,
                                .ir = 0x01,
                            },
                        .mem = {0x01, 0x02, 0x03, 0x4, [HIGH_RAM_START] = 0x12},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_ld_imm16mem_sp) LD [IMM16], SP",
                .init =
                    {
                        .cpu = {.ir = 0x08, .sp = 0x1234},
                        .mem =
                            {
                                HIGH_RAM_START & 0xFF,
                                HIGH_RAM_START >> 8,
                                0x03,
                                0x04,
                            },
                    },
                .want =
                    {
                        .cpu =
                            {
                                .sp = 0x1234,
                                .pc = 3,
                                .ir = 0x03,
                            },
                        .mem =
                            {
                                HIGH_RAM_START & 0xFF,
                                HIGH_RAM_START >> 8,
                                0x03,
                                0x04,
                                [HIGH_RAM_START] = 0x34,
                                [HIGH_RAM_START + 1] = 0x12,
                            },
                    },
                .cycles = 4,
            },
            {
                .name = "(exec_inc_r16) INC BC",
                .init =
                    {
                        .cpu =
                            {
                                .registers = {[REG_B] = 0x00, [REG_C] = 0xFF},
                                .ir = 0x03,
                            },
                        .mem = {0x01, 0x02, 0x03, 0x04},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_B] = 0x01, [REG_C] = 0x00},
                                .pc = 0x01,
                                .ir = 0x01,
                            },
                        .mem = {0x01, 0x02, 0x03, 0x04},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_dec_r16) DEC BC",
                .init =
                    {
                        .cpu =
                            {
                                .registers = {[REG_B] = 0x01, [REG_C] = 0x00},
                                .ir = 0x0B,
                            },
                        .mem = {0x01, 0x02, 0x03, 0x04},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_B] = 0x00, [REG_C] = 0xFF},
                                .pc = 0x01,
                                .ir = 0x01,
                            },
                        .mem = {0x01, 0x02, 0x03, 0x04},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_add_hl_r16) ADD HL, BC (no carry)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x09,
                                .registers =
                                    {
                                        [REG_B] = 0,
                                        [REG_C] = 1,
                                        [REG_H] = 0,
                                        [REG_L] = 0,
                                    },
                                // Ensure the flags are set.
                                .flags = FLAGS_NHC,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers =
                                    {
                                        [REG_B] = 0,
                                        [REG_C] = 1,
                                        [REG_H] = 0,
                                        [REG_L] = 1,
                                    },
                                .flags = 0,
                                .pc = 1,
                                .ir = 0x01,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_add_hl_r16) ADD HL, BC (low carry)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x09,
                                .registers =
                                    {
                                        [REG_B] = 0,
                                        [REG_C] = 1,
                                        // Bit 11 carry.
                                        [REG_H] = 0x0F,
                                        [REG_L] = 0xFF,
                                    },
                                // Ensure the flags are set.
                                .flags = FLAGS_NHC,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers =
                                    {
                                        [REG_B] = 0,
                                        [REG_C] = 1,
                                        [REG_H] = 0x10,
                                        [REG_L] = 0,
                                    },
                                .flags = FLAG_H,
                                .pc = 1,
                                .ir = 0x01,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_add_hl_r16) ADD HL, BC (high carry)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x09,
                                .registers =
                                    {
                                        [REG_B] = 0x80,
                                        [REG_C] = 0,
                                        [REG_H] = 0x80,
                                        [REG_L] = 0,
                                    },
                                // Ensure the flags are set.
                                .flags = FLAGS_NHC,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers =
                                    {
                                        [REG_B] = 0x80,
                                        [REG_C] = 0,
                                        [REG_H] = 0,
                                        [REG_L] = 0,
                                    },
                                // Ensure the flags are set.
                                .flags = FLAG_C,
                                .pc = 1,
                                .ir = 0x01,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_add_hl_r16) ADD HL, BC (carries due to low-carry)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x09,
                                .registers =
                                    {
                                        [REG_B] = 0xFF,
                                        [REG_C] = 1,
                                        [REG_H] = 0xFF,
                                        [REG_L] = 0xFF,
                                    },
                                // Ensure the flags are set.
                                .flags = FLAGS_NHC,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers =
                                    {
                                        [REG_B] = 0xFF,
                                        [REG_C] = 1,
                                        [REG_H] = 0xFF,
                                        [REG_L] = 0,
                                    },
                                // Ensure the flags are set.
                                .flags = FLAG_C | FLAG_H,
                                .pc = 1,
                                .ir = 0x01,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_add_hl_r16) ADD HL, BC (low and high carry)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x09,
                                .registers =
                                    {
                                        [REG_B] = 0,
                                        [REG_C] = 1,
                                        // Bit 15 and 11 carry.
                                        [REG_H] = 0xFF,
                                        [REG_L] = 0xFF,
                                    },
                                // Ensure the flags are set.
                                .flags = FLAGS_NHC,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers =
                                    {
                                        [REG_B] = 0,
                                        [REG_C] = 1,
                                        [REG_H] = 0,
                                        [REG_L] = 0,
                                    },
                                // Ensure the flags are set.
                                .flags = FLAG_H | FLAG_C,
                                .pc = 1,
                                .ir = 0x01,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_inc_r8) INC A (non-zero, no carry)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x3C,
                                .registers = {[REG_A] = 0},
                                // Ensure the flags are set.
                                .flags = FLAGS_ZNH,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_A] = 1},
                                .flags = 0,
                                .pc = 1,
                                .ir = 1,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_inc_r8) INC A (half carry)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x3C,
                                .registers = {[REG_A] = 0xF},
                                // Ensure the flags are set.
                                .flags = FLAGS_ZNH,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_A] = 0x10},
                                .flags = FLAG_H,
                                .pc = 1,
                                .ir = 1,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_inc_r8) INC A (zero)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x3C,
                                .registers = {[REG_A] = 0xFF},
                                // Ensure the flags are set.
                                .flags = FLAGS_ZNH,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_A] = 0},
                                // The only way to get to zero is to increment
                                // 0xFF. This necessitates a half-carry too.
                                .flags = FLAG_Z | FLAG_H,
                                .pc = 1,
                                .ir = 1,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_inc_r8) INC B",
                .init =
                    {
                        .cpu = {.ir = 0x04, .registers = {[REG_B] = 0}},
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_B] = 1},
                                .flags = 0,
                                .pc = 1,
                                .ir = 1,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_inc_r8) INC [HL]",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x34,
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                // Ensure the flags are set.
                                .flags = FLAGS_ZNH,
                            },
                        .mem = {1, 2, 3, 4, [HIGH_RAM_START] = 5},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .flags = 0,
                                .pc = 1,
                                .ir = 1,
                            },
                        .mem = {1, 2, 3, 4, [HIGH_RAM_START] = 6},
                    },
                .cycles = 3,
            },
            {
                .name = "(exec_dec_r8) DEC A (non-zero, no borrow)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x3D,
                                .registers = {[REG_A] = 2},
                                // Ensure the flags are set.
                                .flags = FLAGS_ZNH,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_A] = 1},
                                .flags = FLAG_N,
                                .pc = 1,
                                .ir = 1,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_dec_r8) DEC A (half borrow)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x3D,
                                .registers = {[REG_A] = 0x10},
                                // Ensure the flags are set.
                                .flags = FLAGS_ZNH,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_A] = 0x0F},
                                .flags = FLAG_N | FLAG_H,
                                .pc = 1,
                                .ir = 1,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_dec_r8) DEC A (zero)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x3D,
                                .registers = {[REG_A] = 1},
                                // Ensure the flags are set.
                                .flags = FLAGS_ZNH,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_A] = 0},
                                .flags = FLAG_N | FLAG_Z,
                                .pc = 1,
                                .ir = 1,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_dec_r8) DEC B",
                .init =
                    {
                        .cpu = {.ir = 0x05, .registers = {[REG_B] = 2}},
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_B] = 1},
                                .flags = FLAG_N,
                                .pc = 1,
                                .ir = 1,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_inc_r8) DEC [HL]",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x35,
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                // Ensure the flags are set.
                                .flags = FLAGS_ZNH,
                            },
                        .mem = {1, 2, 3, 4, [HIGH_RAM_START] = 5},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .flags = 0,
                                .pc = 1,
                                .ir = 1,
                            },
                        .mem = {1, 2, 3, 4, [HIGH_RAM_START] = 4},
                    },
                .cycles = 3,
            },
            {
                .name = "(exec_ld_r8_imm8) LD A, imm8",
                .init =
                    {
                        .cpu = {.ir = 0x3E, .registers = {[REG_A] = 0}},
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu = {.registers = {[REG_A] = 1}, .pc = 2, .ir = 2},
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_ld_r8_imm8) LD B, imm8",
                .init =
                    {
                        .cpu = {.ir = 0x06, .registers = {[REG_B] = 0}},
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu = {.registers = {[REG_B] = 1}, .pc = 2, .ir = 2},
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_ld_r8_imm8) LD [HL], imm8",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x36,
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .pc = 2,
                                .ir = 2,
                            },
                        .mem = {1, 2, 3, 4, [HIGH_RAM_START] = 1},
                    },
                .cycles = 3,
            },
            {
                .name = "(exec_rlca) RLCA (no carry)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x07,
                                .registers = {[REG_A] = 0x1},
                                .flags = FLAGS_ZNHC,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_A] = 0x2},
                                .pc = 1,
                                .ir = 1,
                                .flags = 0,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_rlca) RLCA (carry)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x07,
                                .registers = {[REG_A] = 0xAA},
                                .flags = FLAGS_ZNHC,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_A] = 0x55},
                                .flags = FLAG_C,
                                .pc = 1,
                                .ir = 1,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_rrca) RRCA (no carry)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x0F,
                                .registers = {[REG_A] = 0x80},
                                .flags = FLAGS_ZNHC,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_A] = 0x40},
                                .pc = 1,
                                .ir = 1,
                                .flags = 0,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_rrca) RRCA (carry)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x0F,
                                .registers = {[REG_A] = 0x55},
                                .flags = FLAGS_ZNHC,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_A] = 0xAA},
                                .flags = FLAG_C,
                                .pc = 1,
                                .ir = 1,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_rla) RLA (no carry)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x17,
                                .registers = {[REG_A] = 0x01},
                                .flags = FLAGS_ZNH, // no carry
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_A] = 0x02},
                                .pc = 1,
                                .ir = 1,
                                .flags = 0,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_rla) RLA (carry-in)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x17,
                                .registers = {[REG_A] = 0x01},
                                .flags = FLAGS_ZNHC, // yes carry
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_A] = 0x03},
                                .pc = 1,
                                .ir = 1,
                                .flags = 0,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_rrca) RLA (carry-out)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x17,
                                .registers = {[REG_A] = 0xAA},
                                .flags = FLAGS_ZNH, // no carry
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_A] = 0x54},
                                .flags = FLAG_C,
                                .pc = 1,
                                .ir = 1,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_rra) RRA (no carry)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x1F,
                                .registers = {[REG_A] = 0x80},
                                .flags = FLAGS_ZNH, // no carry
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_A] = 0x40},
                                .pc = 1,
                                .ir = 1,
                                .flags = 0,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_rra) RRA (carry-in)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x1F,
                                .registers = {[REG_A] = 0x80},
                                .flags = FLAGS_ZNHC, // yes carry
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_A] = 0xC0},
                                .pc = 1,
                                .ir = 1,
                                .flags = 0,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_rra) RRA (carry-out)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x1F,
                                .registers = {[REG_A] = 0x55},
                                .flags = FLAGS_ZNH, // no carry
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_A] = 0x2A},
                                .flags = FLAG_C,
                                .pc = 1,
                                .ir = 1,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_daa) DAA (N)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x27,
                                .registers = {[REG_A] = 0x01},
                                .flags = FLAG_N,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_A] = 0x01},
                                .flags = FLAG_N,
                                .pc = 1,
                                .ir = 1,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_daa) DAA (NH)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x27,
                                .registers = {[REG_A] = 0x11},
                                .flags = FLAG_N | FLAG_H,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_A] = 0xB /* 0x11-0x6 */},
                                .flags = FLAG_N,
                                .pc = 1,
                                .ir = 1,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_daa) DAA (NC)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x27,
                                .registers = {[REG_A] = 0x01},
                                .flags = FLAG_N | FLAG_C,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_A] = 0xA1 /* 0x1-0x60 */},
                                .flags = FLAG_N | FLAG_C,
                                .pc = 1,
                                .ir = 1,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_daa) DAA (NCH)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x27,
                                .registers = {[REG_A] = 0x11},
                                .flags = FLAG_N | FLAG_C | FLAG_H,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_A] = 0xAB /* 0x11-0x66 */},
                                .flags = FLAG_N | FLAG_C,
                                .pc = 1,
                                .ir = 1,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_daa) DAA (0 flags)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x27,
                                .registers = {[REG_A] = 0x01},
                                .flags = 0,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_A] = 0x01},
                                .flags = 0,
                                .pc = 1,
                                .ir = 1,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_daa) DAA (H)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x27,
                                .registers = {[REG_A] = 0x11},
                                .flags = FLAG_H,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_A] = 0x17 /* 0x11 + 0x6 */},
                                .flags = 0,
                                .pc = 1,
                                .ir = 1,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_daa) DAA (A&F > 9)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x27,
                                .registers = {[REG_A] = 0xA},
                                .flags = 0,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_A] = 0x10 /* 0xA + 0x6 */},
                                .flags = 0,
                                .pc = 1,
                                .ir = 1,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_daa) DAA (C)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x27,
                                .registers = {[REG_A] = 0x1},
                                .flags = FLAG_C,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_A] = 0x61 /* 0x1 + 0x60 */},
                                .flags = FLAG_C,
                                .pc = 1,
                                .ir = 1,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_daa) DAA (a > 0x99)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x27,
                                .registers = {[REG_A] = 0xA1},
                                .flags = 0,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_A] = 0x01 /* 0xA1 + 0x60 */},
                                .flags = FLAG_C,
                                .pc = 1,
                                .ir = 1,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_daa) DAA (CH)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x27,
                                .registers = {[REG_A] = 0x11},
                                .flags = FLAG_C | FLAG_H,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_A] = 0x77 /* 0x11 + 0x66 */},
                                .flags = FLAG_C,
                                .pc = 1,
                                .ir = 1,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_daa) DAA (C A&F > 9)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x27,
                                .registers = {[REG_A] = 0x1A},
                                .flags = FLAG_C,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_A] = 0x80 /* 0x1A + 0x66 */},
                                .flags = FLAG_C,
                                .pc = 1,
                                .ir = 1,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_daa) DAA (H A > 0x99)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x27,
                                .registers = {[REG_A] = 0xAA},
                                .flags = FLAG_H,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_A] = 0x10 /* 0x1A + 0x66 */},
                                .flags = FLAG_C,
                                .pc = 1,
                                .ir = 1,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_daa) DAA (set Z)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x27,
                                .registers = {[REG_A] = 0x06},
                                .flags = FLAG_H | FLAG_N,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_A] = 0},
                                .flags = FLAG_Z | FLAG_N,
                                .pc = 1,
                                .ir = 1,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_cpl) CPL",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x2F,
                                .registers = {[REG_A] = 0x00},
                                .flags = 0,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_A] = 0xFF},
                                .flags = FLAG_N | FLAG_H,
                                .pc = 1,
                                .ir = 1,
                            },
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_scf) SCF",
                .init =
                    {
                        .cpu = {.ir = 0x37, .flags = FLAG_N | FLAG_H},
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu = {.flags = FLAG_C, .pc = 1, .ir = 1},
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_ccf) CCF !true",
                .init =
                    {
                        .cpu = {.ir = 0x3F, .flags = FLAG_N | FLAG_H | FLAG_C},
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu = {.flags = 0, .pc = 1, .ir = 1},
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_ccf) CCF !false",
                .init =
                    {
                        .cpu = {.ir = 0x3F, .flags = FLAG_N | FLAG_H},
                        .mem = {1, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu = {.flags = FLAG_C, .pc = 1, .ir = 1},
                        .mem = {1, 2, 3, 4},
                    },
                .cycles = 1,
            },
            {
                .name = "(exec_rlc_r8) RLC B (no carry, non-zero)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers = {[REG_B] = 0x1},
                                .flags = FLAGS_ZNHC,
                            },
                        .mem = {/* op code */ 0x00, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_B] = 0x2},
                                .pc = 2,
                                .ir = 2,
                                .flags = 0,
                            },
                        .mem = {/* op code */ 0x00, 2, 3, 4},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_rlc_r8) RLC B (carry)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers = {[REG_B] = 0x80},
                                .flags = FLAGS_ZNH, /* carry not set */
                            },
                        .mem = {/* op code */ 0x00, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_B] = 0x1},
                                .pc = 2,
                                .ir = 2,
                                .flags = FLAG_C,
                            },
                        .mem = {/* op code */ 0x00, 2, 3, 4},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_rlc_r8) RLC B (zero)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers = {[REG_B] = 0x00},
                                .flags = FLAGS_NHC, /* zero not set */
                            },
                        .mem = {/* op code */ 0x00, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_B] = 0x0},
                                .pc = 2,
                                .ir = 2,
                                .flags = FLAG_Z,
                            },
                        .mem = {/* op code */ 0x00, 2, 3, 4},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_rlc_r8) RLC [HL]",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .flags = FLAGS_ZNHC,
                            },
                        .mem = {
                            /* op code */ 0x06,
                            2,
                            3,
                            4,
                            [HIGH_RAM_START] = 0x88,
                        },
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .pc = 2,
                                .ir = 2,
                                .flags = FLAG_C,
                            },
                        .mem = {
                            /* op code */ 0x06,
                            2,
                            3,
                            4,
                            [HIGH_RAM_START] = 0x11,
                        },
                    },
                .cycles = 4,
            },
            {
                .name = "(exec_rrc_r8) RRC B (no carry, non-zero)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers = {[REG_B] = 0x10},
                                .flags = FLAGS_ZNHC,
                            },
                        .mem = {/* op code */ 0x08, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_B] = 0x08},
                                .pc = 2,
                                .ir = 2,
                                .flags = 0,
                            },
                        .mem = {/* op code */ 0x08, 2, 3, 4},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_rrc_r8) RRC B (carry)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers = {[REG_B] = 0x01},
                                .flags = FLAGS_ZNH, /* carry not set */
                            },
                        .mem = {/* op code */ 0x08, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_B] = 0x80},
                                .pc = 2,
                                .ir = 2,
                                .flags = FLAG_C,
                            },
                        .mem = {/* op code */ 0x08, 2, 3, 4},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_rrc_r8) RRC B (zero)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers = {[REG_B] = 0x00},
                                .flags = FLAGS_NHC, /* zero not set */
                            },
                        .mem = {/* op code */ 0x08, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_B] = 0x0},
                                .pc = 2,
                                .ir = 2,
                                .flags = FLAG_Z,
                            },
                        .mem = {/* op code */ 0x08, 2, 3, 4},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_rrc_r8) RRC [HL]",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .flags = FLAGS_ZNHC,
                            },
                        .mem = {
                            /* op code */ 0x0E,
                            2,
                            3,
                            4,
                            [HIGH_RAM_START] = 0x01,
                        },
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .pc = 2,
                                .ir = 2,
                                .flags = FLAG_C,
                            },
                        .mem = {
                            /* op code */ 0x0E,
                            2,
                            3,
                            4,
                            [HIGH_RAM_START] = 0x80,
                        },
                    },
                .cycles = 4,
            },
            {
                .name = "(exec_rl_r8) RL B (no carry, non-zero)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers = {[REG_B] = 0x10},
                                .flags = FLAGS_ZNHC,
                            },
                        .mem = {/* op code */ 0x10, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_B] = 0x21},
                                .pc = 2,
                                .ir = 2,
                                .flags = 0,
                            },
                        .mem = {/* op code */ 0x10, 2, 3, 4},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_rl_r8) RL B (carry, zero)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers = {[REG_B] = 0x80},
                                .flags =
                                    FLAGS_NH, /* carry not set; zero not set */
                            },
                        .mem = {/* op code */ 0x10, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_B] = 0x00},
                                .pc = 2,
                                .ir = 2,
                                .flags = FLAG_C | FLAG_Z,
                            },
                        .mem = {/* op code */ 0x10, 2, 3, 4},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_rl_r8) RL [HL]",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .flags = FLAGS_ZNH, /* no carry */
                            },
                        .mem = {
                            /* op code */ 0x16,
                            2,
                            3,
                            4,
                            [HIGH_RAM_START] = 0x80,
                        },
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .pc = 2,
                                .ir = 2,
                                .flags = FLAG_C | FLAG_Z,
                            },
                        .mem = {
                            /* op code */ 0x16,
                            2,
                            3,
                            4,
                            [HIGH_RAM_START] = 0x00,
                        },
                    },
                .cycles = 4,
            },
            {
                .name = "(exec_rr_r8) RR B (no carry, non-zero)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers = {[REG_B] = 0x10},
                                .flags = FLAGS_ZNHC,
                            },
                        .mem = {/* op code */ 0x18, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_B] = 0x88},
                                .pc = 2,
                                .ir = 2,
                                .flags = 0,
                            },
                        .mem = {/* op code */ 0x18, 2, 3, 4},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_rr_r8) RR B (carry, zero)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers = {[REG_B] = 0x01},
                                .flags =
                                    FLAGS_NH, /* carry not set; zero not set */
                            },
                        .mem = {/* op code */ 0x18, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_B] = 0x00},
                                .pc = 2,
                                .ir = 2,
                                .flags = FLAG_C | FLAG_Z,
                            },
                        .mem = {/* op code */ 0x18, 2, 3, 4},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_rr_r8) RR [HL]",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .flags = FLAGS_ZNH, /* no carry */
                            },
                        .mem = {
                            /* op code */ 0x1E,
                            2,
                            3,
                            4,
                            [HIGH_RAM_START] = 0x01,
                        },
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .pc = 2,
                                .ir = 2,
                                .flags = FLAG_C | FLAG_Z,
                            },
                        .mem = {
                            /* op code */ 0x1E,
                            2,
                            3,
                            4,
                            [HIGH_RAM_START] = 0x00,
                        },
                    },
                .cycles = 4,
            },
            {
                .name = "(exec_sla_r8) SLA B",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers = {[REG_B] = 0x01},
                                .flags = FLAGS_ZNH,
                            },
                        .mem = {/* op code */ 0x20, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_B] = 0x02},
                                .pc = 2,
                                .ir = 2,
                                .flags = 0,
                            },
                        .mem = {/* op code */ 0x20, 2, 3, 4},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_sla_r8) SLA B (carry, zero)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers = {[REG_B] = 0x80},
                                .flags = FLAGS_NH,
                            },
                        .mem = {/* op code */ 0x20, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_B] = 0x00},
                                .pc = 2,
                                .ir = 2,
                                .flags = FLAG_C | FLAG_Z,
                            },
                        .mem = {/* op code */ 0x20, 2, 3, 4},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_sla_r8) SLA [HL]",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .flags = FLAGS_ZNH,
                            },
                        .mem = {
                            /* op code */ 0x26,
                            2,
                            3,
                            4,
                            [HIGH_RAM_START] = 0x01,
                        },
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .pc = 2,
                                .ir = 2,
                                .flags = 0,
                            },
                        .mem = {
                            /* op code */ 0x26,
                            2,
                            3,
                            4,
                            [HIGH_RAM_START] = 0x02,
                        },
                    },
                .cycles = 4,
            },
            {
                .name = "(exec_sra_r8) SRA B (high bit is zero)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers = {[REG_B] = 0x02},
                                .flags = FLAGS_ZNH,
                            },
                        .mem = {/* op code */ 0x28, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_B] = 0x1},
                                .pc = 2,
                                .ir = 2,
                                .flags = 0,
                            },
                        .mem = {/* op code */ 0x28, 2, 3, 4},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_sra_r8) SRA B (high bit is one)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers = {[REG_B] = 0x80},
                                .flags = FLAGS_ZNH,
                            },
                        .mem = {/* op code */ 0x28, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_B] = 0xC0},
                                .pc = 2,
                                .ir = 2,
                                .flags = 0,
                            },
                        .mem = {/* op code */ 0x28, 2, 3, 4},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_sra_r8) SRA B (carry, zero)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers = {[REG_B] = 0x01},
                                .flags = FLAGS_NH,
                            },
                        .mem = {/* op code */ 0x28, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_B] = 0x00},
                                .pc = 2,
                                .ir = 2,
                                .flags = FLAG_C | FLAG_Z,
                            },
                        .mem = {/* op code */ 0x28, 2, 3, 4},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_sra_r8) SRA [HL]",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .flags = FLAGS_ZNH,
                            },
                        .mem = {
                            /* op code */ 0x2E,
                            2,
                            3,
                            4,
                            [HIGH_RAM_START] = 0x02,
                        },
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .pc = 2,
                                .ir = 2,
                                .flags = 0,
                            },
                        .mem = {
                            /* op code */ 0x2E,
                            2,
                            3,
                            4,
                            [HIGH_RAM_START] = 0x01,
                        },
                    },
                .cycles = 4,
            },
            {
                .name = "(exec_swap_r8) SWAP A",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers = {[REG_A] = 0xA5},
                                .flags = FLAGS_NH,
                            },
                        .mem = {/* op code */ 0x37, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_A] = 0x5A},
                                .pc = 2,
                                .ir = 2,
                                .flags = 0,
                            },
                        .mem = {/* op code */ 0x37, 2, 3, 4},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_swap_r8) SWAP B",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers = {[REG_B] = 0xA5},
                                .flags = FLAGS_ZNHC, // carry flag set
                            },
                        .mem = {/* op code */ 0x30, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_B] = 0x5A},
                                .pc = 2,
                                .ir = 2,
                                .flags = 0, // clears the carry flag
                            },
                        .mem = {/* op code */ 0x30, 2, 3, 4},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_swap_r8) SWAP B (zero)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers = {[REG_B] = 0x00},
                                .flags = FLAGS_NH,
                            },
                        .mem = {/* op code */ 0x30, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_B] = 0x00},
                                .pc = 2,
                                .ir = 2,
                                .flags = FLAG_Z,
                            },
                        .mem = {/* op code */ 0x30, 2, 3, 4},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_swap_r8) SWAP [HL]",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .flags = FLAGS_ZNH,
                            },
                        .mem = {
                            /* op code */ 0x36,
                            2,
                            3,
                            4,
                            [HIGH_RAM_START] = 0x5A,
                        },
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .pc = 2,
                                .ir = 2,
                                .flags = 0,
                            },
                        .mem = {
                            /* op code */ 0x36,
                            2,
                            3,
                            4,
                            [HIGH_RAM_START] = 0xA5,
                        },
                    },
                .cycles = 4,
            },
            {
                .name = "(exec_srl_r8) SRL B",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers = {[REG_B] = 0x80},
                                .flags = FLAGS_ZNHC, // carry flag set
                            },
                        .mem = {/* op code */ 0x38, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_B] = 0x40},
                                .pc = 2,
                                .ir = 2,
                                .flags = 0, // clears the carry flag
                            },
                        .mem = {/* op code */ 0x38, 2, 3, 4},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_srl_r8) SRL B (carry, zero)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers = {[REG_B] = 0x01},
                                .flags = FLAGS_NH,
                            },
                        .mem = {/* op code */ 0x38, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_B] = 0x00},
                                .pc = 2,
                                .ir = 2,
                                .flags = FLAG_C | FLAG_Z,
                            },
                        .mem = {/* op code */ 0x38, 2, 3, 4},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_srl_r8) SRL [HL]",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .flags = FLAGS_ZNH,
                            },
                        .mem = {
                            /* op code */ 0x3E,
                            2,
                            3,
                            4,
                            [HIGH_RAM_START] = 0x80,
                        },
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .pc = 2,
                                .ir = 2,
                                .flags = 0,
                            },
                        .mem = {
                            /* op code */ 0x3E,
                            2,
                            3,
                            4,
                            [HIGH_RAM_START] = 0x40,
                        },
                    },
                .cycles = 4,
            },
            {
                .name = "(exec_bit_b3_r8) BIT 2 B (1)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers = {[REG_B] = 0x04},
                                .flags = FLAG_Z,
                            },
                        .mem = {/* op code */ 0x50, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_B] = 0x04},
                                .pc = 2,
                                .ir = 2,
                                .flags = 0,
                            },
                        .mem = {/* op code */ 0x50, 2, 3, 4},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_bit_b3_r8) BIT 2 B (0)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers = {[REG_B] = ~0x04},
                                .flags = 0,
                            },
                        .mem = {/* op code */ 0x50, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_B] = ~0x04},
                                .pc = 2,
                                .ir = 2,
                                .flags = FLAG_Z,
                            },
                        .mem = {/* op code */ 0x50, 2, 3, 4},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_bit_b3_r8) BIT 2 [HL]",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .flags = FLAG_Z,
                            },
                        .mem = {
                            /* op code */ 0x56,
                            2,
                            3,
                            4,
                            [HIGH_RAM_START] = 0x04,
                        },
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .pc = 2,
                                .ir = 2,
                                .flags = 0,
                            },
                        .mem = {
                            /* op code */ 0x56,
                            2,
                            3,
                            4,
                            [HIGH_RAM_START] = 0x04,
                        },
                    },
                .cycles = 3,
            },
            {
                .name = "(exec_res_b3_r8) RES 2 B",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers = {[REG_B] = 0x04},
                            },
                        .mem = {/* op code */ 0x90, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_B] = 0x00},
                                .pc = 2,
                                .ir = 2,
                            },
                        .mem = {/* op code */ 0x90, 2, 3, 4},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_res_b3_r8) RES 2 [HL]",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                            },
                        .mem = {
                            /* op code */ 0x96,
                            2,
                            3,
                            4,
                            [HIGH_RAM_START] = 0x04,
                        },
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .pc = 2,
                                .ir = 2,
                            },
                        .mem = {
                            /* op code */ 0x96,
                            2,
                            3,
                            4,
                            [HIGH_RAM_START] = 0x00,
                        },
                    },
                .cycles = 4,
            },
            {
                .name = "(exec_set_b3_r8) SET 2 B",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers = {[REG_B] = 0x00},
                            },
                        .mem = {/* op code */ 0xD0, 2, 3, 4},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers = {[REG_B] = 0x04},
                                .pc = 2,
                                .ir = 2,
                            },
                        .mem = {/* op code */ 0xD0, 2, 3, 4},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_set_b3_r8) SET 2 [HL]",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xCB, // Op-code is at mem[pc == 0].
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                            },
                        .mem = {
                            /* op code */ 0xd6,
                            2,
                            3,
                            4,
                            [HIGH_RAM_START] = 0x00,
                        },
                    },
                .want =
                    {
                        .cpu =
                            {
                                .registers =
                                    {
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .pc = 2,
                                .ir = 2,
                            },
                        .mem = {
                            /* op code */ 0xD6,
                            2,
                            3,
                            4,
                            [HIGH_RAM_START] = 0x04,
                        },
                    },
                .cycles = 4,
            },
            {
                .name = "(exec_jr_imm8) JR 0",
                .init =
                    {
                        .cpu = {.pc = 0, .ir = 0x18},
                        .mem =
                            {
                                [0] = 0, // pc
                                [1] = 1,
                                [2] = 2,
                                [3] = 3,
                            },
                    },
                .want =
                    {
                        .cpu = {.pc = 2, .ir = 1},
                        .mem =
                            {
                                [0] = 0,
                                [1] = 1,
                                [2] = 2,
                                [3] = 3,
                            },
                    },
                .cycles = 3,
            },
            {
                .name = "(exec_jr_imm8) JR 1",
                .init =
                    {
                        .cpu = {.pc = 0, .ir = 0x18},
                        .mem =
                            {
                                [0] = 1, // pc
                                [1] = 1,
                                [2] = 2,
                                [3] = 3,
                            },
                    },
                .want =
                    {
                        .cpu = {.pc = 3, .ir = 2},
                        .mem =
                            {
                                [0] = 1,
                                [1] = 1,
                                [2] = 2,
                                [3] = 3,
                            },
                    },
                .cycles = 3,
            },
            {
                .name = "(exec_jr_imm8) JR 127",
                .init =
                    {
                        .cpu = {.pc = 0, .ir = 0x18},
                        .mem =
                            {
                                [0] = 127,
                                [128] = 5,
                            },
                    },
                .want =
                    {
                        .cpu = {.pc = 129, .ir = 5},
                        .mem =
                            {
                                [0] = 127,
                                [128] = 5,
                            },
                    },
                .cycles = 3,
            },
            {
                .name = "(exec_jr_imm8) JR -1",
                .init =
                    {
                        .cpu = {.pc = 1, .ir = 0x18},
                        .mem =
                            {
                                [0] = 0,
                                [1] = -1, // pc
                            },
                    },
                .want =
                    {
                        .cpu = {.pc = 2, .ir = -1},
                        .mem =
                            {
                                [0] = 0,
                                [1] = -1,
                            },
                    },
                .cycles = 3,
            },
            {
                .name = "(exec_jr_imm8) JR -128",
                .init =
                    {
                        .cpu = {.pc = 200, .ir = 0x18},
                        .mem =
                            {
                                [73] = 5,
                                [200] = -128,
                            },
                    },
                .want =
                    {
                        .cpu = {.pc = 74, .ir = 5},
                        .mem =
                            {
                                [73] = 5,
                                [200] = -128,
                            },
                    },
                .cycles = 3,
            },
            {
                .name = "(exec_jr_imm8) JR 128 (JR -128)",
                .init =
                    {
                        .cpu = {.pc = 200, .ir = 0x18},
                        .mem =
                            {
                                [73] = 5,
                                [200] = 128,
                            },
                    },
                .want =
                    {
                        .cpu = {.pc = 74, .ir = 5},
                        .mem =
                            {
                                [73] = 5,
                                [200] = 128,
                            },
                    },
                .cycles = 3,
            },
            {
                .name = "(exec_jr_cond_imm8) JR NZ 1 (true)",
                .init =
                    {
                        .cpu = {.pc = 2, .ir = 0x20, .flags = 0},
                        .mem = {0, 1, /* imm8=*/1, 3, 4},

                    },
                .want =
                    {
                        .cpu = {.pc = 5, .ir = 4, .flags = 0},
                        .mem = {0, 1, 1, 3, 4},

                    },
                .cycles = 3,
            },
            {
                .name = "(exec_jr_imm8) JR NZ -4 (true)",
                .init =
                    {
                        .cpu = {.pc = 0x026E, .ir = 0x20},
                        .mem =
                            {
                                [0x026B] = 0xAA,
                                [0x026D] = 0x20,
                                [0x026E] = 0xFC, // -4
                            },
                    },
                .want =
                    {
                        .cpu = {.pc = 0x026C, .ir = 0xAA},
                        .mem =
                            {
                                [0x026B] = 0xAA,
                                [0x026D] = 0x20,
                                [0x026E] = 0xFC, // -4
                            },
                    },
                .cycles = 3,
            },
            {
                .name = "(exec_jr_imm8) JR NZ -4 (false)",
                .init =
                    {
                        .cpu = {.pc = 0x026E, .ir = 0x20, .flags = FLAG_Z},
                        .mem =
                            {
                                [0x026D] = 0x20,
                                [0x026E] = 0xFC, // -4
                                [0x026F] = 0xAA,
                            },
                    },
                .want =
                    {
                        .cpu = {.pc = 0x0270, .ir = 0xAA, .flags = FLAG_Z},
                        .mem =
                            {
                                [0x026D] = 0x20,
                                [0x026E] = 0xFC, // -4
                                [0x026F] = 0xAA,
                            },
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_jr_cond_imm8) JR NZ 1 (false)",
                .init =
                    {
                        .cpu = {.pc = 2, .ir = 0x20, .flags = FLAG_Z},
                        .mem = {0, 1, /* imm8=*/1, 3, 4},

                    },
                .want =
                    {
                        .cpu = {.pc = 4, .ir = 3, .flags = FLAG_Z},
                        .mem = {0, 1, 1, 3, 4},

                    },
                .cycles = 2,
            },
            {
                .name = "(exec_jr_cond_imm8) JR Z 1 (true)",
                .init =
                    {
                        .cpu = {.pc = 2, .ir = 0x28, .flags = FLAG_Z},
                        .mem = {0, 1, /* imm8=*/1, 3, 4},

                    },
                .want =
                    {
                        .cpu = {.pc = 5, .ir = 4, .flags = FLAG_Z},
                        .mem = {0, 1, 1, 3, 4},

                    },
                .cycles = 3,
            },
            {
                .name = "(exec_jr_cond_imm8) JR Z 1 (false)",
                .init =
                    {
                        .cpu = {.pc = 2, .ir = 0x28, .flags = 0},
                        .mem = {0, 1, /* imm8=*/1, 3, 4},

                    },
                .want =
                    {
                        .cpu = {.pc = 4, .ir = 3, .flags = 0},
                        .mem = {0, 1, 1, 3, 4},

                    },
                .cycles = 2,
            },
            {
                .name = "(exec_jr_cond_imm8) JR NC 1 (true)",
                .init =
                    {
                        .cpu = {.pc = 2, .ir = 0x30, .flags = 0},
                        .mem = {0, 1, /* imm8=*/1, 3, 4},

                    },
                .want =
                    {
                        .cpu = {.pc = 5, .ir = 4, .flags = 0},
                        .mem = {0, 1, 1, 3, 4},

                    },
                .cycles = 3,
            },
            {
                .name = "(exec_jr_cond_imm8) JR NC 1 (false)",
                .init =
                    {
                        .cpu = {.pc = 2, .ir = 0x30, .flags = FLAG_C},
                        .mem = {0, 1, /* imm8=*/1, 3, 4},

                    },
                .want =
                    {
                        .cpu = {.pc = 4, .ir = 3, .flags = FLAG_C},
                        .mem = {0, 1, 1, 3, 4},

                    },
                .cycles = 2,
            },
            {
                .name = "(exec_jr_cond_imm8) JR C 1 (true)",
                .init =
                    {
                        .cpu = {.pc = 2, .ir = 0x38, .flags = FLAG_C},
                        .mem = {0, 1, /* imm8=*/1, 3, 4},

                    },
                .want =
                    {
                        .cpu = {.pc = 5, .ir = 4, .flags = FLAG_C},
                        .mem = {0, 1, 1, 3, 4},

                    },
                .cycles = 3,
            },
            {
                .name = "(exec_jr_cond_imm8) JR C 1 (false)",
                .init =
                    {
                        .cpu = {.pc = 2, .ir = 0x38, .flags = 0},
                        .mem = {0, 1, /* imm8=*/1, 3, 4},

                    },
                .want =
                    {
                        .cpu = {.pc = 4, .ir = 3, .flags = 0},
                        .mem = {0, 1, 1, 3, 4},

                    },
                .cycles = 2,
            },
            {
                .name = "(exec_jr_cond_imm8) JR NZ -1 (true)",
                .init =
                    {
                        .cpu = {.pc = 2, .ir = 0x20, .flags = 0},
                        .mem = {0, 1, /* imm8=*/-1, 3, 4},

                    },
                .want =
                    {
                        .cpu = {.pc = 3, .ir = -1, .flags = 0},
                        .mem = {0, 1, -1, 3, 4},

                    },
                .cycles = 3,
            },
            {
                .name = "(exec_ld_r8_r8) LD B, B",
                .init = {.cpu = {.ir = 0x40, .registers = {[REG_B] = 2}}},
                .want = {.cpu = {.pc = 1, .registers = {[REG_B] = 2}}},
                .cycles = 1,
            },
            {
                .name = "(exec_ld_r8_r8) LD B, C",
                .init = {.cpu = {.ir = 0x41,
                                 .registers = {[REG_B] = 0, [REG_C] = 2}}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_B] = 2, [REG_C] = 2}}},
                .cycles = 1,
            },
            {
                .name = "(exec_ld_r8_r8) LD B, D",
                .init = {.cpu = {.ir = 0x42,
                                 .registers = {[REG_B] = 0, [REG_D] = 2}}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_B] = 2, [REG_D] = 2}}},
                .cycles = 1,
            },
            {
                .name = "(exec_ld_r8_r8) LD B, E",
                .init = {.cpu = {.ir = 0x43,
                                 .registers = {[REG_B] = 0, [REG_E] = 2}}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_B] = 2, [REG_E] = 2}}},
                .cycles = 1,
            },
            {
                .name = "(exec_ld_r8_r8) LD B, H",
                .init = {.cpu = {.ir = 0x44,
                                 .registers = {[REG_B] = 0, [REG_H] = 2}}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_B] = 2, [REG_H] = 2}}},
                .cycles = 1,
            },
            {
                .name = "(exec_ld_r8_r8) LD B, L",
                .init = {.cpu = {.ir = 0x45,
                                 .registers = {[REG_B] = 0, [REG_L] = 2}}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_B] = 2, [REG_L] = 2}}},
                .cycles = 1,
            },
            {
                .name = "(exec_ld_r8_r8) LD B, [HL]",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x46,
                                .registers =
                                    {
                                        [REG_B] = 0,
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                            },
                        .mem = {[HIGH_RAM_START] = 2},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .pc = 1,
                                .registers =
                                    {
                                        [REG_B] = 2,
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                            },
                        .mem = {[HIGH_RAM_START] = 2},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_ld_r8_r8) LD B, A",
                .init = {.cpu = {.ir = 0x47,
                                 .registers = {[REG_B] = 0, [REG_A] = 2}}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_B] = 2, [REG_A] = 2}}},
                .cycles = 1,
            },
            {
                .name = "(exec_ld_r8_r8) LD C, B",
                .init = {.cpu = {.ir = 0x48, .registers = {[REG_B] = 2}}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_C] = 2, [REG_B] = 2}}},
                .cycles = 1,
            },
            {
                .name = "(exec_ld_r8_r8) LD D, B",
                .init = {.cpu = {.ir = 0x50,
                                 .registers = {[REG_D] = 0, [REG_B] = 2}}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_D] = 2, [REG_B] = 2}}},
                .cycles = 1,
            },
            {
                .name = "(exec_ld_r8_r8) LD E, B",
                .init = {.cpu = {.ir = 0x58,
                                 .registers = {[REG_E] = 0, [REG_B] = 2}}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_E] = 2, [REG_B] = 2}}},
                .cycles = 1,
            },
            {
                .name = "(exec_ld_r8_r8) LD H, B",
                .init = {.cpu = {.ir = 0x60,
                                 .registers = {[REG_H] = 0, [REG_B] = 2}}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_H] = 2, [REG_B] = 2}}},
                .cycles = 1,
            },
            {
                .name = "(exec_ld_r8_r8) LD L, B",
                .init = {.cpu = {.ir = 0x68,
                                 .registers = {[REG_L] = 0, [REG_B] = 2}}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_L] = 2, [REG_B] = 2}}},
                .cycles = 1,
            },
            {
                .name = "(exec_ld_r8_r8) LD [HL], B",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x70,
                                .registers =
                                    {
                                        [REG_B] = 2,
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                            },
                        .mem = {[HIGH_RAM_START] = 0},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .pc = 1,
                                .registers =
                                    {
                                        [REG_B] = 2,
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                            },
                        .mem = {[HIGH_RAM_START] = 2},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_ld_r8_r8) LD A, B",
                .init = {.cpu = {.ir = 0x78,
                                 .registers = {[REG_A] = 0, [REG_B] = 2}}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_A] = 2, [REG_B] = 2}}},
                .cycles = 1,
            },
            {
                .name = "(exec_add_a_r8) ADD A, B",
                .init =
                    {.cpu =
                         {.ir = 0x80,
                          .registers = {[REG_A] = 1, [REG_B] = 2},
                          .flags = FLAG_N /* should clear N */ |
                                   FLAG_C /* shouldn't add C to the result */}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_A] = 3, [REG_B] = 2},
                                 .flags = 0}},
                .cycles = 1,
            },
            {
                .name = "(exec_add_a_r8) ADD A, B (half carry)",
                .init = {.cpu = {.ir = 0x80,
                                 .registers = {[REG_A] = 1, [REG_B] = 0xF}}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_A] = 0x10, [REG_B] = 0xF},
                                 .flags = FLAG_H}},
                .cycles = 1,
            },
            {
                .name = "(exec_add_a_r8) ADD A, B (carry)",
                .init = {.cpu =
                             {.ir = 0x80,
                              .registers = {[REG_A] = 0xF1, [REG_B] = 0x80}}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_A] = 0x71, [REG_B] = 0x80},
                                 .flags = FLAG_C}},
                .cycles = 1,
            },
            {
                .name = "(exec_add_a_r8) ADD A, B (carry and half_carry)",
                .init = {.cpu =
                             {.ir = 0x80,
                              .registers = {[REG_A] = 0xFF, [REG_B] = 0x81}}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_A] = 0x80, [REG_B] = 0x81},
                                 .flags = FLAG_C | FLAG_H}},
                .cycles = 1,
            },
            {
                .name = "(exec_add_a_r8) ADD A, B (zero)",
                .init = {.cpu = {.ir = 0x80,
                                 .registers = {[REG_A] = 0, [REG_B] = 0}}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_A] = 0, [REG_B] = 0},
                                 .flags = FLAG_Z}},
                .cycles = 1,
            },
            {
                .name = "(exec_add_a_r8) ADD A, [HL]",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x86,
                                .registers =
                                    {
                                        [REG_A] = 1,
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .flags =
                                    FLAG_N /* should clear N */ |
                                    FLAG_C /* shouldn't add C to the result */,
                            },
                        .mem = {[HIGH_RAM_START] = 2},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .pc = 1,
                                .registers =
                                    {
                                        [REG_A] = 3,
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                            },
                        .mem = {[HIGH_RAM_START] = 2},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_adc_a_r8) ADC A, B (carry in)",
                .init = {.cpu =
                             {
                                 .ir = 0x88,
                                 .registers = {[REG_A] = 1, [REG_B] = 2},
                                 .flags = FLAG_C | FLAG_N /* should clear N */
                             }},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_A] = 4, [REG_B] = 2},
                                 .flags = 0}},
                .cycles = 1,
            },
            {
                .name = "(exec_adc_a_r8) ADC A, B (no carry in)",
                .init = {.cpu =
                             {
                                 .ir = 0x88,
                                 .registers = {[REG_A] = 1, [REG_B] = 2},
                                 .flags = FLAG_N /* should clear N */
                             }},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_A] = 3, [REG_B] = 2},
                                 .flags = 0}},
                .cycles = 1,
            },
            {
                .name = "(exec_adc_a_r8) ADC A, B (half carry)",
                .init = {.cpu = {.ir = 0x88,
                                 .registers = {[REG_A] = 0, [REG_B] = 0xF},
                                 .flags = FLAG_C}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_A] = 0x10, [REG_B] = 0xF},
                                 .flags = FLAG_H}},
                .cycles = 1,
            },
            {
                .name = "(exec_adc_a_r8) ADC A, B (carry)",
                .init = {.cpu = {.ir = 0x88,
                                 .registers = {[REG_A] = 0xF0, [REG_B] = 0x80},
                                 .flags = FLAG_C}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_A] = 0x71, [REG_B] = 0x80},
                                 .flags = FLAG_C}},
                .cycles = 1,
            },
            {
                .name = "(exec_adc_a_r8) ADC A, B (carry and half_carry)",
                .init = {.cpu = {.ir = 0x88,
                                 .registers = {[REG_A] = 0xFF, [REG_B] = 0x80},
                                 .flags = FLAG_C}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_A] = 0x80, [REG_B] = 0x80},
                                 .flags = FLAG_C | FLAG_H}},
                .cycles = 1,
            },
            {
                .name = "(exec_adc_a_r8) ADC A, B (zero)",
                .init = {.cpu = {.ir = 0x88,
                                 .registers = {[REG_A] = 0, [REG_B] = 0}}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_A] = 0, [REG_B] = 0},
                                 .flags = FLAG_Z}},
                .cycles = 1,
            },
            {
                .name = "(exec_adc_a_r8) ADC A, [HL]",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x8E,
                                .registers =
                                    {
                                        [REG_A] = 1,
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .flags = FLAG_N /* should clear N */ | FLAG_C,
                            },
                        .mem = {[HIGH_RAM_START] = 2},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .pc = 1,
                                .registers =
                                    {
                                        [REG_A] = 4,
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                            },
                        .mem = {[HIGH_RAM_START] = 2},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_sub_a_r8) SUB A, B",
                .init = {.cpu = {.ir = 0x90,
                                 .registers = {[REG_A] = 3, [REG_B] = 1}}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_A] = 2, [REG_B] = 1},
                                 .flags = FLAG_N}},
                .cycles = 1,
            },
            {
                .name = "(exec_sub_a_r8) SUB A, B (half borrow)",
                .init = {.cpu = {.ir = 0x90,
                                 .registers = {[REG_A] = 0x10, [REG_B] = 1}}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_A] = 0xF, [REG_B] = 1},
                                 .flags = FLAG_N | FLAG_H}},
                .cycles = 1,
            },
            {
                .name = "(exec_sub_a_r8) SUB A, B (borrow)",
                .init = {.cpu = {.ir = 0x90,
                                 .registers = {[REG_A] = 0x1, [REG_B] = 2}}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_A] = 0xFF, [REG_B] = 2},
                                 .flags = FLAG_N | FLAG_C}},
                .cycles = 1,
            },
            {
                .name = "(exec_sub_a_r8) SUB A, B (zero)",
                .init = {.cpu = {.ir = 0x90,
                                 .registers = {[REG_A] = 2, [REG_B] = 2}}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_A] = 0, [REG_B] = 2},
                                 .flags = FLAG_N | FLAG_Z}},
                .cycles = 1,
            },
            {
                .name = "(exec_sub_a_r8) SUB A, [HL]",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x96,
                                .registers =
                                    {
                                        [REG_A] = 3,
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .flags = FLAG_C,
                            },
                        .mem = {[HIGH_RAM_START] = 1},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .pc = 1,
                                .registers =
                                    {
                                        [REG_A] = 2,
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .flags = FLAG_N,
                            },
                        .mem = {[HIGH_RAM_START] = 1},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_sbc_a_r8) SBC A, B (carry in)",
                .init = {.cpu = {.ir = 0x98,
                                 .registers = {[REG_A] = 4, [REG_B] = 2},
                                 .flags = FLAG_C}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_A] = 1, [REG_B] = 2},
                                 .flags = FLAG_N}},
                .cycles = 1,
            },
            {
                .name = "(exec_sbc_a_r8) SBC A, B (no carry in)",
                .init = {.cpu = {.ir = 0x98,
                                 .registers = {[REG_A] = 4, [REG_B] = 2},
                                 .flags = 0}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_A] = 2, [REG_B] = 2},
                                 .flags = FLAG_N}},
                .cycles = 1,
            },
            {
                .name = "(exec_sbc_a_r8) SBC A, B (half-borrow)",
                .init = {.cpu = {.ir = 0x98,
                                 .registers = {[REG_A] = 0x20, [REG_B] = 0x10},
                                 .flags = FLAG_C}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_A] = 0xF, [REG_B] = 0x10},
                                 .flags = FLAG_N | FLAG_H}},
                .cycles = 1,
            },
            {
                .name = "(exec_sbc_a_r8) SBC A, B (borrow)",
                .init = {.cpu = {.ir = 0x98,
                                 .registers = {[REG_A] = 2, [REG_B] = 2},
                                 .flags = FLAG_C}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_A] = 0xFF, [REG_B] = 2},
                                 .flags = FLAG_N | FLAG_C}},
                .cycles = 1,
            },
            {
                .name = "(exec_sbc_a_r8) SBC A, B (zero)",
                .init = {.cpu = {.ir = 0x98,
                                 .registers = {[REG_A] = 2, [REG_B] = 1},
                                 .flags = FLAG_C}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_A] = 0, [REG_B] = 1},
                                 .flags = FLAG_N | FLAG_Z}},
                .cycles = 1,
            },
            {
                .name = "(exec_sbc_a_r8) SBC A, [HL]",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0x9E,
                                .registers =
                                    {
                                        [REG_A] = 4,
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .flags = FLAG_C,
                            },
                        .mem = {[HIGH_RAM_START] = 1},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .pc = 1,
                                .registers =
                                    {
                                        [REG_A] = 2,
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .flags = FLAG_N,
                            },
                        .mem = {[HIGH_RAM_START] = 1},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_and_a_r8) AND A, B",
                .init = {.cpu = {.ir = 0xA0,
                                 .registers = {[REG_A] = 0xFF, [REG_B] = 0xAA},
                                 .flags = FLAG_N | FLAG_C | FLAG_Z}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_A] = 0xAA, [REG_B] = 0xAA},
                                 .flags = FLAG_H}},
                .cycles = 1,
            },
            {
                .name = "(exec_and_a_r8) AND A, B (zero)",
                .init = {.cpu = {.ir = 0xA0,
                                 .registers = {[REG_A] = 0x55, [REG_B] = 0xAA},
                                 .flags = FLAG_N | FLAG_C}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_A] = 0, [REG_B] = 0xAA},
                                 .flags = FLAG_H | FLAG_Z}},
                .cycles = 1,
            },
            {
                .name = "(exec_and_a_r8) AND A, [HL]",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xA6,
                                .registers =
                                    {
                                        [REG_A] = 0xFF,
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                            },
                        .mem = {[HIGH_RAM_START] = 0xAA},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .pc = 1,
                                .registers =
                                    {
                                        [REG_A] = 0xAA,
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .flags = FLAG_H,
                            },
                        .mem = {[HIGH_RAM_START] = 0xAA},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_xor_a_r8) XOR A, B",
                .init = {.cpu = {.ir = 0xA8,
                                 .registers = {[REG_A] = 0xF0, [REG_B] = 0xFF},
                                 .flags = FLAG_N | FLAG_C | FLAG_Z | FLAG_H}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_A] = 0x0F, [REG_B] = 0xFF},
                                 .flags = 0}},
                .cycles = 1,
            },
            {
                .name = "(exec_xor_a_r8) XOR A, B (zero)",
                .init = {.cpu = {.ir = 0xA8,
                                 .registers = {[REG_A] = 0xFF, [REG_B] = 0xFF},
                                 .flags = FLAG_N | FLAG_C | FLAG_H}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_A] = 0, [REG_B] = 0xFF},
                                 .flags = FLAG_Z}},
                .cycles = 1,
            },
            {
                .name = "(exec_xor_a_r8) XOR A, [HL]",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xAE,
                                .registers =
                                    {
                                        [REG_A] = 0xFF,
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .flags = FLAG_Z | FLAG_N | FLAG_H | FLAG_C,
                            },
                        .mem = {[HIGH_RAM_START] = 0xAA},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .pc = 1,
                                .registers =
                                    {
                                        [REG_A] = 0x55,
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .flags = 0,
                            },
                        .mem = {[HIGH_RAM_START] = 0xAA},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_or_a_r8) OR A, B",
                .init = {.cpu = {.ir = 0xB0,
                                 .registers = {[REG_A] = 0xF0, [REG_B] = 0x0F},
                                 .flags = FLAG_N | FLAG_C | FLAG_Z | FLAG_H}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_A] = 0xFF, [REG_B] = 0x0F},
                                 .flags = 0}},
                .cycles = 1,
            },
            {
                .name = "(exec_or_a_r8) OR A, B (zero)",
                .init = {.cpu = {.ir = 0xB0,
                                 .registers = {[REG_A] = 0, [REG_B] = 0},
                                 .flags = FLAG_N | FLAG_C | FLAG_H}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_A] = 0, [REG_B] = 0},
                                 .flags = FLAG_Z}},
                .cycles = 1,
            },
            {
                .name = "(exec_or_a_r8) OR A, [HL]",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xB6,
                                .registers =
                                    {
                                        [REG_A] = 0xF0,
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .flags = FLAG_Z | FLAG_N | FLAG_H | FLAG_C,
                            },
                        .mem = {[HIGH_RAM_START] = 0x0F},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .pc = 1,
                                .registers =
                                    {
                                        [REG_A] = 0xFF,
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .flags = 0,
                            },
                        .mem = {[HIGH_RAM_START] = 0x0F},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_cp_a_r8) CP A, B",
                .init = {.cpu = {.ir = 0xB8,
                                 .registers = {[REG_A] = 3, [REG_B] = 1}}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_A] = 3, [REG_B] = 1},
                                 .flags = FLAG_N}},
                .cycles = 1,
            },
            {
                .name = "(exec_cp_a_r8) CP A, B (half borrow)",
                .init = {.cpu = {.ir = 0xB8,
                                 .registers = {[REG_A] = 0x10, [REG_B] = 1}}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_A] = 0x10, [REG_B] = 1},
                                 .flags = FLAG_N | FLAG_H}},
                .cycles = 1,
            },
            {
                .name = "(exec_cp_a_r8) CP A, B (borrow)",
                .init = {.cpu = {.ir = 0xB8,
                                 .registers = {[REG_A] = 0x1, [REG_B] = 2}}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_A] = 0x1, [REG_B] = 2},
                                 .flags = FLAG_N | FLAG_C}},
                .cycles = 1,
            },
            {
                .name = "(exec_cp_a_r8) CP A, B (zero)",
                .init = {.cpu = {.ir = 0xB8,
                                 .registers = {[REG_A] = 2, [REG_B] = 2}}},
                .want = {.cpu = {.pc = 1,
                                 .registers = {[REG_A] = 2, [REG_B] = 2},
                                 .flags = FLAG_N | FLAG_Z}},
                .cycles = 1,
            },
            {
                .name = "(exec_cp_a_r8) CP A, [HL]",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xBE,
                                .registers =
                                    {
                                        [REG_A] = 3,
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .flags = FLAG_C,
                            },
                        .mem = {[HIGH_RAM_START] = 1},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .pc = 1,
                                .registers =
                                    {
                                        [REG_A] = 3,
                                        [REG_H] = HIGH_RAM_START >> 8,
                                        [REG_L] = HIGH_RAM_START & 0xFF,
                                    },
                                .flags = FLAG_N,
                            },
                        .mem = {[HIGH_RAM_START] = 1},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_add_a_imm8) ADD A, imm8",
                .init =
                    {
                        .cpu = {.ir = 0xC6,
                                .registers = {[REG_A] = 1},
                                .flags = FLAG_N | FLAG_C},
                        .mem = {2},
                    },
                .want =
                    {
                        .cpu = {.pc = 2,
                                .registers = {[REG_A] = 3},
                                .flags = 0},
                        .mem = {2},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_adc_a_imm8) ADC A, imm8",
                .init =
                    {
                        .cpu = {.ir = 0xCE,
                                .registers = {[REG_A] = 1},
                                .flags = FLAG_N | FLAG_C},
                        .mem = {2},
                    },
                .want =
                    {
                        .cpu = {.pc = 2,
                                .registers = {[REG_A] = 4},
                                .flags = 0},
                        .mem = {2},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_sub_a_imm8) SUB A, imm8",
                .init =
                    {
                        .cpu = {.ir = 0xD6,
                                .registers = {[REG_A] = 4},
                                .flags = FLAG_C},
                        .mem = {2},
                    },
                .want =
                    {
                        .cpu = {.pc = 2,
                                .registers = {[REG_A] = 2},
                                .flags = FLAG_N},
                        .mem = {2},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_sbc_a_imm8) SBC A, imm8",
                .init =
                    {
                        .cpu = {.ir = 0xDE,
                                .registers = {[REG_A] = 4},
                                .flags = FLAG_C},
                        .mem = {2},
                    },
                .want =
                    {
                        .cpu = {.pc = 2,
                                .registers = {[REG_A] = 1},
                                .flags = FLAG_N},
                        .mem = {2},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_and_a_imm8) AND A, imm8",
                .init =
                    {
                        .cpu = {.ir = 0xE6,
                                .registers = {[REG_A] = 0xFF},
                                .flags = FLAG_N | FLAG_C},
                        .mem = {0xF},
                    },
                .want =
                    {
                        .cpu = {.pc = 2,
                                .registers = {[REG_A] = 0xF},
                                .flags = FLAG_H},
                        .mem = {0xF},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_xor_a_imm8) XOR A, imm8",
                .init =
                    {
                        .cpu = {.ir = 0xEE,
                                .registers = {[REG_A] = 0xFF},
                                .flags = FLAG_N | FLAG_C | FLAG_H},
                        .mem = {0xF},
                    },
                .want =
                    {
                        .cpu = {.pc = 2,
                                .registers = {[REG_A] = 0xF0},
                                .flags = 0},
                        .mem = {0xF},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_or_a_imm8) OR A, imm8",
                .init =
                    {
                        .cpu = {.ir = 0xF6,
                                .registers = {[REG_A] = 0xAA},
                                .flags = FLAG_N | FLAG_C | FLAG_H},
                        .mem = {0x55},
                    },
                .want =
                    {
                        .cpu = {.pc = 2,
                                .registers = {[REG_A] = 0xFF},
                                .flags = 0},
                        .mem = {0x55},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_cp_a_imm8) CP A, imm8",
                .init =
                    {
                        .cpu = {.ir = 0xFE,
                                .registers = {[REG_A] = 4},
                                .flags = FLAG_C},
                        .mem = {5},
                    },
                .want =
                    {
                        .cpu = {.pc = 2,
                                .registers = {[REG_A] = 4},
                                .flags = FLAG_N | FLAG_C},
                        .mem = {5},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_ret_cond) RET NZ (not taken)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xC0,
                                .flags = FLAG_Z,
                                .sp = 1,
                            },
                        .mem =
                            {
                                0,
                                HIGH_RAM_START & 0xFF,
                                HIGH_RAM_START >> 8,
                                [HIGH_RAM_START] = 5,
                            },
                    },
                .want =
                    {
                        .cpu =
                            {
                                .ir = 0,
                                .pc = 1,
                                .flags = FLAG_Z,
                                .sp = 1,
                            },
                        .mem =
                            {
                                0,
                                HIGH_RAM_START & 0xFF,
                                HIGH_RAM_START >> 8,
                                [HIGH_RAM_START] = 5,
                            },
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_ret_cond) RET NZ (taken)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xC0,
                                .flags = 0,
                                .sp = 1,
                            },
                        .mem =
                            {
                                0,
                                HIGH_RAM_START & 0xFF,
                                HIGH_RAM_START >> 8,
                                [HIGH_RAM_START] = 5,
                            },
                    },
                .want =
                    {
                        .cpu =
                            {
                                .ir = 5,
                                .pc = HIGH_RAM_START + 1,
                                .sp = 3,
                            },
                        .mem =
                            {
                                0,
                                HIGH_RAM_START & 0xFF,
                                HIGH_RAM_START >> 8,
                                [HIGH_RAM_START] = 5,
                            },
                    },
                .cycles = 5,
            },
            {
                .name = "(exec_ret) RET",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xC9,
                                .sp = 1,
                            },
                        .mem =
                            {
                                0,
                                HIGH_RAM_START & 0xFF,
                                HIGH_RAM_START >> 8,
                                [HIGH_RAM_START] = 5,
                            },
                    },
                .want =
                    {
                        .cpu =
                            {
                                .ir = 5,
                                .pc = HIGH_RAM_START + 1,
                                .sp = 3,
                            },
                        .mem =
                            {
                                0,
                                HIGH_RAM_START & 0xFF,
                                HIGH_RAM_START >> 8,
                                [HIGH_RAM_START] = 5,
                            },
                    },
                .cycles = 4,
            },
            {
                .name = "(exec_reti) RETI",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xD9,
                                .sp = 1,
                            },
                        .mem =
                            {
                                0,
                                HIGH_RAM_START & 0xFF,
                                HIGH_RAM_START >> 8,
                                [HIGH_RAM_START] = 5,
                            },
                    },
                .want =
                    {
                        .cpu =
                            {
                                .ir = 5,
                                .pc = HIGH_RAM_START + 1,
                                .sp = 3,
                                .ime = 1,
                            },
                        .mem =
                            {
                                0,
                                HIGH_RAM_START & 0xFF,
                                HIGH_RAM_START >> 8,
                                [HIGH_RAM_START] = 5,
                            },
                    },
                .cycles = 4,
            },
            {
                .name = "(exec_jp_cond_imm16) JP NZ HIGH_RAM_START (not taken)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xC2,
                                .flags = FLAG_Z,
                            },
                        .mem =
                            {
                                HIGH_RAM_START & 0xFF,
                                HIGH_RAM_START >> 8,
                                [HIGH_RAM_START] = 5,
                            },
                    },
                .want =
                    {
                        .cpu =
                            {
                                .ir = 0,
                                .pc = 3,
                                .flags = FLAG_Z,
                            },
                        .mem =
                            {
                                HIGH_RAM_START & 0xFF,
                                HIGH_RAM_START >> 8,
                                [HIGH_RAM_START] = 5,
                            },
                    },
                .cycles = 3,
            },
            {
                .name = "(exec_jp_cond_imm16) JP NZ HIGH_RAM_START (taken)",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xC2,
                                .flags = 0,
                            },
                        .mem =
                            {
                                HIGH_RAM_START & 0xFF,
                                HIGH_RAM_START >> 8,
                                [HIGH_RAM_START] = 5,
                            },
                    },
                .want =
                    {
                        .cpu =
                            {
                                .ir = 5,
                                .pc = HIGH_RAM_START + 1,
                            },
                        .mem =
                            {
                                HIGH_RAM_START & 0xFF,
                                HIGH_RAM_START >> 8,
                                [HIGH_RAM_START] = 5,
                            },
                    },
                .cycles = 4,
            },
            {
                .name = "(exec_jp_imm16) JP HIGH_RAM_START",
                .init =
                    {
                        .cpu = {.ir = 0xC3},
                        .mem =
                            {
                                HIGH_RAM_START & 0xFF,
                                HIGH_RAM_START >> 8,
                                [HIGH_RAM_START] = 5,
                            },
                    },
                .want =
                    {
                        .cpu = {.ir = 5, .pc = HIGH_RAM_START + 1},
                        .mem =
                            {
                                HIGH_RAM_START & 0xFF,
                                HIGH_RAM_START >> 8,
                                [HIGH_RAM_START] = 5,
                            },
                    },
                .cycles = 4,
            },
            {
                .name = "(exec_jp_hl) JP HL",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xE9,
                                .registers = {[REG_H] = HIGH_RAM_START >> 8,
                                              [REG_L] = HIGH_RAM_START & 0xFF},
                            },
                        .mem = {[HIGH_RAM_START] = 5},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .ir = 5,
                                .pc = HIGH_RAM_START + 1,
                                .registers = {[REG_H] = HIGH_RAM_START >> 8,
                                              [REG_L] = HIGH_RAM_START & 0xFF},
                            },
                        .mem = {[HIGH_RAM_START] = 5},
                    },
                .cycles = 1,
            },
            {
                .name =
                    "(exec_call_cond_imm16) CALL NZ HIGH_RAM_START (not taken)",
                .init =
                    {
                        .cpu = {.ir = 0xC4, .sp = 0xFFFE, .flags = FLAG_Z},
                        .mem =
                            {
                                HIGH_RAM_START & 0xFF,
                                HIGH_RAM_START >> 8,
                                5,
                                [HIGH_RAM_START] = 0,
                            },
                    },
                .want =
                    {
                        .cpu =
                            {.ir = 5, .pc = 3, .sp = 0xFFFE, .flags = FLAG_Z},
                        .mem =
                            {
                                HIGH_RAM_START & 0xFF,
                                HIGH_RAM_START >> 8,
                                5,
                                [HIGH_RAM_START] = 0,
                            },
                    },
                .cycles = 3,
            },
            {
                .name = "(exec_call_cond_imm16) CALL NZ HIGH_RAM_START (taken)",
                .init =
                    {
                        .cpu = {.ir = 0xC4, .sp = 0xFFFE},
                        .mem =
                            {
                                HIGH_RAM_START & 0xFF,
                                HIGH_RAM_START >> 8,
                                0,
                                [HIGH_RAM_START] = 5,
                            },
                    },
                .want =
                    {
                        .cpu =
                            {
                                .ir = 5,
                                .pc = HIGH_RAM_START + 1,
                                .sp = 0xFFFC,
                            },
                        .mem =
                            {
                                HIGH_RAM_START & 0xFF,
                                HIGH_RAM_START >> 8,
                                0,
                                [HIGH_RAM_START] = 5,
                                // pc = 2
                                [0xFFFC] = 2,
                                [0xFFFD] = 0,
                            },
                    },
                .cycles = 6,
            },
            {
                .name = "(exec_call_imm16) CALL HIGH_RAM_START",
                .init =
                    {
                        .cpu = {.ir = 0xCD, .sp = 0xFFFE},
                        .mem =
                            {
                                HIGH_RAM_START & 0xFF,
                                HIGH_RAM_START >> 8,
                                0,
                                [HIGH_RAM_START] = 5,
                            },
                    },
                .want =
                    {
                        .cpu =
                            {
                                .ir = 5,
                                .pc = HIGH_RAM_START + 1,
                                .sp = 0xFFFC,
                            },
                        .mem =
                            {
                                HIGH_RAM_START & 0xFF,
                                HIGH_RAM_START >> 8,
                                0,
                                [HIGH_RAM_START] = 5,
                                // pc = 2
                                [0xFFFC] = 2,
                                [0xFFFD] = 0,
                            },
                    },
                .cycles = 6,
            },
            {
                .name = "(exec_rst_tgt3) RST $00",
                .init =
                    {
                        .cpu = {.ir = 0xC7, .pc = 12, .sp = 0xFFFE},
                        .mem = {[0x00] = 5, [0xFFFC] = 1, [0xFFFD] = 0},
                    },
                .want =
                    {
                        .cpu = {.ir = 5, .pc = 0x00 + 1, .sp = 0xFFFC},
                        .mem = {[0x00] = 5, [0xFFFC] = 12, [0xFFFD] = 0},
                    },
                .cycles = 4,
            },
            {
                .name = "(exec_rst_tgt3) RST $08",
                .init =
                    {
                        .cpu = {.ir = 0xCF, .pc = 12, .sp = 0xFFFE},
                        .mem = {[0x08] = 5, [0xFFFC] = 1, [0xFFFD] = 0},
                    },
                .want =
                    {
                        .cpu = {.ir = 5, .pc = 0x08 + 1, .sp = 0xFFFC},
                        .mem = {[0x08] = 5, [0xFFFC] = 12, [0xFFFD] = 0},
                    },
                .cycles = 4,
            },
            {
                .name = "(exec_rst_tgt3) RST $10",
                .init =
                    {
                        .cpu = {.ir = 0xD7, .pc = 12, .sp = 0xFFFE},
                        .mem = {[0x10] = 5, [0xFFFC] = 1, [0xFFFD] = 0},
                    },
                .want =
                    {
                        .cpu = {.ir = 5, .pc = 0x10 + 1, .sp = 0xFFFC},
                        .mem = {[0x10] = 5, [0xFFFC] = 12, [0xFFFD] = 0},
                    },
                .cycles = 4,
            },
            {
                .name = "(exec_rst_tgt3) RST $18",
                .init =
                    {
                        .cpu = {.ir = 0xDF, .pc = 12, .sp = 0xFFFE},
                        .mem = {[0x18] = 5, [0xFFFC] = 1, [0xFFFD] = 0},
                    },
                .want =
                    {
                        .cpu = {.ir = 5, .pc = 0x18 + 1, .sp = 0xFFFC},
                        .mem = {[0x18] = 5, [0xFFFC] = 12, [0xFFFD] = 0},
                    },
                .cycles = 4,
            },
            {
                .name = "(exec_rst_tgt3) RST $20",
                .init =
                    {
                        .cpu = {.ir = 0xE7, .pc = 12, .sp = 0xFFFE},
                        .mem = {[0x20] = 5, [0xFFFC] = 1, [0xFFFD] = 0},
                    },
                .want =
                    {
                        .cpu = {.ir = 5, .pc = 0x20 + 1, .sp = 0xFFFC},
                        .mem = {[0x20] = 5, [0xFFFC] = 12, [0xFFFD] = 0},
                    },
                .cycles = 4,
            },
            {
                .name = "(exec_rst_tgt3) RST $28",
                .init =
                    {
                        .cpu = {.ir = 0xEF, .pc = 12, .sp = 0xFFFE},
                        .mem = {[0x28] = 5, [0xFFFC] = 1, [0xFFFD] = 0},
                    },
                .want =
                    {
                        .cpu = {.ir = 5, .pc = 0x28 + 1, .sp = 0xFFFC},
                        .mem = {[0x28] = 5, [0xFFFC] = 12, [0xFFFD] = 0},
                    },
                .cycles = 4,
            },
            {
                .name = "(exec_rst_tgt3) RST $30",
                .init =
                    {
                        .cpu = {.ir = 0xF7, .pc = 12, .sp = 0xFFFE},
                        .mem = {[0x30] = 5, [0xFFFC] = 1, [0xFFFD] = 0},
                    },
                .want =
                    {
                        .cpu = {.ir = 5, .pc = 0x30 + 1, .sp = 0xFFFC},
                        .mem = {[0x30] = 5, [0xFFFC] = 12, [0xFFFD] = 0},
                    },
                .cycles = 4,
            },
            {
                .name = "(exec_rst_tgt3) RST $30",
                .init =
                    {
                        .cpu = {.ir = 0xFF, .pc = 12, .sp = 0xFFFE},
                        .mem = {[0x38] = 5, [0xFFFC] = 1, [0xFFFD] = 0},
                    },
                .want =
                    {
                        .cpu = {.ir = 5, .pc = 0x38 + 1, .sp = 0xFFFC},
                        .mem = {[0x38] = 5, [0xFFFC] = 12, [0xFFFD] = 0},
                    },
                .cycles = 4,
            },
            {
                .name = "(exec_pop_r16) POP BC",
                .init =
                    {
                        .cpu = {.ir = 0xC1, .sp = 0xFFFD},
                        .mem = {[0xFFFD] = 1, [0xFFFE] = 2},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .pc = 1,
                                .sp = 0xFFFF,
                                .registers = {[REG_B] = 2, [REG_C] = 1},
                            },
                        .mem = {[0xFFFD] = 1, [0xFFFE] = 2},
                    },
                .cycles = 3,
            },
            {
                .name = "(exec_pop_r16) POP DE",
                .init =
                    {
                        .cpu = {.ir = 0xD1, .sp = 0xFFFD},
                        .mem = {[0xFFFD] = 1, [0xFFFE] = 2},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .pc = 1,
                                .sp = 0xFFFF,
                                .registers = {[REG_D] = 2, [REG_E] = 1},
                            },
                        .mem = {[0xFFFD] = 1, [0xFFFE] = 2},
                    },
                .cycles = 3,
            },
            {
                .name = "(exec_pop_r16) POP HL",
                .init =
                    {
                        .cpu = {.ir = 0xE1, .sp = 0xFFFD},
                        .mem = {[0xFFFD] = 1, [0xFFFE] = 2},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .pc = 1,
                                .sp = 0xFFFF,
                                .registers = {[REG_H] = 2, [REG_L] = 1},
                            },
                        .mem = {[0xFFFD] = 1, [0xFFFE] = 2},
                    },
                .cycles = 3,
            },
            {
                .name = "(exec_pop_r16) POP AF (Z)",
                .init =
                    {
                        .cpu = {.ir = 0xF1, .sp = 0xFFFD},
                        .mem = {[0xFFFD] = FLAG_Z, [0xFFFE] = 2},
                    },
                .want =
                    {
                        .cpu =
                            {
                                .pc = 1,
                                .sp = 0xFFFF,
                                .registers = {[REG_A] = 2},
                                .flags = FLAG_Z,
                            },
                        .mem = {[0xFFFD] = FLAG_Z, [0xFFFE] = 2},
                    },
                .cycles = 3,
            },
            {
                .name = "(exec_push_r16) PUSH BC",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xC5,
                                .sp = 0xFFFF,
                                .registers = {[REG_B] = 2, [REG_C] = 1},
                            },
                    },
                .want =
                    {
                        .cpu =
                            {
                                .pc = 1,
                                .sp = 0xFFFD,
                                .registers = {[REG_B] = 2, [REG_C] = 1},
                            },
                        .mem = {[0xFFFD] = 1, [0xFFFE] = 2},
                    },
                .cycles = 4,
            },
            {
                .name = "(exec_push_r16) PUSH DE",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xD5,
                                .sp = 0xFFFF,
                                .registers = {[REG_D] = 2, [REG_E] = 1},
                            },
                    },
                .want =
                    {
                        .cpu =
                            {
                                .pc = 1,
                                .sp = 0xFFFD,
                                .registers = {[REG_D] = 2, [REG_E] = 1},
                            },
                        .mem = {[0xFFFD] = 1, [0xFFFE] = 2},
                    },
                .cycles = 4,
            },
            {
                .name = "(exec_push_r16) PUSH DE",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xE5,
                                .sp = 0xFFFF,
                                .registers = {[REG_H] = 2, [REG_L] = 1},
                            },
                    },
                .want =
                    {
                        .cpu =
                            {
                                .pc = 1,
                                .sp = 0xFFFD,
                                .registers = {[REG_H] = 2, [REG_L] = 1},
                            },
                        .mem = {[0xFFFD] = 1, [0xFFFE] = 2},
                    },
                .cycles = 4,
            },
            {
                .name = "(exec_push_r16) PUSH AF",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xF5,
                                .sp = 0xFFFF,
                                .registers = {[REG_A] = 2},
                                .flags = FLAG_Z,
                            },
                    },
                .want =
                    {
                        .cpu = {.pc = 1,
                                .sp = 0xFFFD,
                                .registers = {[REG_A] = 2},
                                .flags = FLAG_Z},
                        .mem = {[0xFFFD] = FLAG_Z, [0xFFFE] = 2},
                    },
                .cycles = 4,
            },
            {
                .name = "(exec_ldh_cmem_a) LDH [C], A",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xE2,
                                .registers = {[REG_A] = 2, [REG_C] = 0x80},
                            },
                    },
                .want =
                    {
                        .cpu =
                            {
                                .pc = 1,
                                .registers = {[REG_A] = 2,
                                              [REG_C] = HIGH_RAM_START & 0xFF},
                            },
                        .mem = {[HIGH_RAM_START] = 2},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_ldh_imm8mem_a) LDH [imm8], A",
                .init =
                    {
                        .cpu = {.ir = 0xE0, .registers = {[REG_A] = 2}},
                        .mem = {HIGH_RAM_START & 0xFF},
                    },
                .want =
                    {
                        .cpu = {.pc = 2, .registers = {[REG_A] = 2}},
                        .mem =
                            {
                                HIGH_RAM_START & 0xFF,
                                [HIGH_RAM_START] = 2,
                            },
                    },
                .cycles = 3,
            },
            {
                .name = "(exec_ld_imm16mem_a) LD [imm16], A",
                .init =
                    {
                        .cpu = {.ir = 0xEA, .registers = {[REG_A] = 3}},
                        .mem =
                            {
                                HIGH_RAM_START & 0xFF,
                                HIGH_RAM_START >> 8,
                            },
                    },
                .want =
                    {
                        .cpu = {.pc = 3, .registers = {[REG_A] = 3}},
                        .mem =
                            {
                                HIGH_RAM_START & 0xFF,
                                HIGH_RAM_START >> 8,
                                [HIGH_RAM_START] = 3,
                            },
                    },
                .cycles = 4,
            },
            {
                .name = "(exec_ldh_a_cmem) LDH A, [C]",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xF2,
                                .registers = {[REG_A] = 3, [REG_C] = 4},
                            },
                        .mem = {[0xFF04] = 5},
                    },
                .want =
                    {
                        .cpu = {.pc = 1,
                                .registers = {[REG_A] = 5, [REG_C] = 4}},
                        .mem = {[0xFF04] = 5},
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_ldh_a_imm8mem) LDH A, [imm8]",
                .init =
                    {
                        .cpu = {.ir = 0xF0, .registers = {[REG_A] = 3}},
                        .mem = {4, [0xFF04] = 5},
                    },
                .want =
                    {
                        .cpu = {.pc = 2, .registers = {[REG_A] = 5}},
                        .mem = {4, [0xFF04] = 5},
                    },
                .cycles = 3,
            },
            {
                .name = "(exec_ld_a_imm16mem) LD A, [imm16]",
                .init =
                    {
                        .cpu = {.ir = 0xFA, .registers = {[REG_A] = 3}},
                        .mem = {2, 1, [0x0102] = 5},
                    },
                .want =
                    {
                        .cpu = {.pc = 3, .registers = {[REG_A] = 5}},
                        .mem = {2, 1, [0x0102] = 5},
                    },
                .cycles = 4,
            },
            {
                .name = "(exec_add_sp_imm8) ADD SP, 1",
                .init = {.cpu = {.ir = 0xE8}, .mem = {1}},
                .want = {.cpu = {.pc = 2, .sp = 1}, .mem = {1}},
                .cycles = 4,
            },
            {
                .name = "(exec_add_sp_imm8) ADD SP, -1",
                .init = {.cpu = {.ir = 0xE8}, .mem = {-1}},
                .want = {.cpu = {.pc = 2, .sp = 0xFFFF}, .mem = {-1}},
                .cycles = 4,
            },
            {
                .name = "(exec_add_sp_imm8) ADD SP, -128",
                .init = {.cpu = {.ir = 0xE8}, .mem = {-128}},
                .want = {.cpu = {.pc = 2, .sp = 0xFF80}, .mem = {-128}},
                .cycles = 4,
            },
            {
                .name = "(exec_add_sp_imm8) ADD SP, imm8 (half carry)",
                .init = {.cpu = {.ir = 0xE8, .sp = 0xF}, .mem = {1}},
                .want =
                    {
                        .cpu = {.pc = 2, .sp = 0x10, .flags = FLAG_H},
                        .mem = {1},
                    },
                .cycles = 4,
            },
            {
                .name = "(exec_add_sp_imm8) ADD SP, imm8 (carry)",
                .init = {.cpu = {.ir = 0xE8, .sp = 0xF0}, .mem = {0x10}},
                .want =
                    {
                        .cpu = {.pc = 2, .sp = 0x0100, .flags = FLAG_C},
                        .mem = {0x10},
                    },
                .cycles = 4,
            },
            {
                .name =
                    "(exec_add_sp_imm8) ADD SP, imm8 (carry and half carry)",
                .init = {.cpu = {.ir = 0xE8, .sp = 0xFF}, .mem = {0x11}},
                .want =
                    {
                        .cpu =
                            {
                                .pc = 2,
                                .sp = 0x0110,
                                .flags = FLAG_C | FLAG_H,
                            },
                        .mem = {0x11},
                    },
                .cycles = 4,
            },
            {
                .name = "(exec_ld_hl_sp_plus_imm8) LD HL, SP+1",
                .init = {.cpu = {.ir = 0xF8, .sp = 0x0101}, .mem = {1}},
                .want =
                    {
                        .cpu =
                            {
                                .pc = 2,
                                .sp = 0x0101,
                                .registers = {[REG_H] = 0x01, [REG_L] = 2},
                            },
                        .mem = {1},
                    },
                .cycles = 3,
            },
            {
                .name = "(exec_ld_hl_sp_plus_imm8) LD HL, SP-1",
                .init = {.cpu = {.ir = 0xF8, .sp = 0}, .mem = {-1}},
                .want =
                    {
                        .cpu =
                            {
                                .pc = 2,
                                .sp = 0,
                                .registers = {[REG_H] = 0xFF, [REG_L] = 0xFF},
                            },
                        .mem = {-1},
                    },
                .cycles = 3,
            },
            {
                .name = "(exec_ld_hl_sp_plus_imm8) LD HL, SP-128",
                .init = {.cpu = {.ir = 0xF8, .sp = 0}, .mem = {-128}},
                .want =
                    {
                        .cpu =
                            {
                                .pc = 2,
                                .sp = 0,
                                .registers = {[REG_H] = 0xFF, [REG_L] = 0x80},
                            },
                        .mem = {-128},
                    },
                .cycles = 3,
            },
            {
                .name = "(exec_ld_hl_sp_plus_imm8) LD HL, SP+n (half carry)",
                .init = {.cpu = {.ir = 0xF8, .sp = 0xF}, .mem = {1}},
                .want =
                    {
                        .cpu =
                            {
                                .pc = 2,
                                .sp = 0xF,
                                .registers = {[REG_H] = 0, [REG_L] = 0x10},
                                .flags = FLAG_H,
                            },
                        .mem = {1},
                    },
                .cycles = 3,
            },
            {
                .name = "(exec_ld_hl_sp_plus_imm8) LD HL, SP+n (carry)",
                .init = {.cpu = {.ir = 0xF8, .sp = 0xF0}, .mem = {0x10}},
                .want =
                    {
                        .cpu =
                            {
                                .pc = 2,
                                .sp = 0xF0,
                                .registers = {[REG_H] = 0x01, [REG_L] = 0x00},
                                .flags = FLAG_C,
                            },
                        .mem = {0x10},
                    },
                .cycles = 3,
            },
            {
                .name = "(exec_ld_hl_sp_plus_imm8) LD HL, SP+n (carry and half "
                        "carry)",
                .init = {.cpu = {.ir = 0xF8, .sp = 0xFF}, .mem = {0x11}},
                .want =
                    {
                        .cpu =
                            {
                                .pc = 2,
                                .sp = 0xFF,
                                .registers = {[REG_H] = 0x01, [REG_L] = 0x10},
                                .flags = FLAG_C | FLAG_H,
                            },
                        .mem = {0x11},
                    },
                .cycles = 3,
            },
            {
                .name = "(exec_ld_sp_hl) LD SP, HL",
                .init =
                    {
                        .cpu =
                            {
                                .ir = 0xF9,
                                .registers =
                                    {
                                        [REG_H] = 0xF,
                                        [REG_L] = 0xA,
                                    },
                            }},
                .want =
                    {
                        .cpu =
                            {
                                .pc = 1,
                                .sp = 0x0F0A,
                                .registers =
                                    {
                                        [REG_H] = 0xF,
                                        [REG_L] = 0xA,
                                    },
                            },
                    },
                .cycles = 2,
            },
            {
                .name = "(exec_di) DI",
                .init = {.cpu = {.ir = 0xF3, .ime = 1}},
                .want = {.cpu = {.pc = 1, .ime = 0}},
                .cycles = 1,
            },
            {
                .name = "(exec_di) DI cancels EI",
                .init = {.cpu = {.ir = 0xF3, .ime = 0, .ei_pend = true}},
                .want = {.cpu = {.pc = 1, .ime = 0}},
                .cycles = 1,
            },
            {
                .name = "(exec_di) EI",
                .init = {.cpu = {.ir = 0xFB, .ime = 0}},
                .want = {.cpu = {.pc = 1, .ime = 0, .ei_pend = true}},
                .cycles = 1,
            },
            {
                // Let's make sure that find_mem_region can find the highest
                // region.
                .name = "LD [0xFFFF], A",
                .init =
                    {
                        .cpu = {.ir = 0xEA, .registers = {[REG_A] = 3}},
                        .mem = {0xFF, 0xFF},
                    },
                .want =
                    {
                        .cpu = {.pc = 3, .registers = {[REG_A] = 3}},
                        .mem = {0xFF, 0xFF, [0xFFFF] = 0x03},
                    },
                .cycles = 4,
            },
};

void _run_exec_tests(struct exec_test exec_tests[], int n) {
  for (int i = 0; i < n; i++) {
    struct exec_test *test = &exec_tests[i];
    Gameboy g = test->init;
    g.rom = &test->rom;
    int cycles = step(&g);
    if (cycles != test->cycles) {
      FAIL("%s: got %d cycles, expected %d", test->name, cycles, test->cycles);
    }
    char *diff = gameboy_diff(&g, &test->want);
    if (diff != NULL) {
      FAIL("%s: Gameboy state does not match expected\n: %s", test->name, diff);
    }
  }
}

void run_exec_tests() { _run_exec_tests(exec_tests, ARRAY_SIZE(exec_tests)); }

void run_ei_delayed_test() {
  Gameboy g = {.cpu = {.ir = 0xFB /* EI */}, .mem = {NOP}};
  step(&g); // Execute first EI in the IR.
  if (g.cpu.ime) {
    FAIL("EI set the IME right away");
  }
  step(&g); // Execute NOP at address 0.
  if (!g.cpu.ime) {
    FAIL("NOP after EI did not set IME");
  }
}

void run_ei_di_test() {
  Gameboy g = {.cpu = {.ir = EI}, .mem = {EI, DI, NOP}};
  step(&g); // Execute first EI in the IR.
  if (g.cpu.ime) {
    FAIL("EI set the IME right away");
  }
  step(&g); // Execute EI at address 0.
  if (g.cpu.ime) {
    FAIL("EI after EI set the IME right away");
  }
  step(&g); // Execute DI at address 1.
  if (g.cpu.ime) {
    FAIL("DI after EI set the IME");
  }
  step(&g); // Execute NOP at address 2.
  if (g.cpu.ime) {
    FAIL("NOP after DI set the IME");
  }
}

static struct exec_test call_interrupt_tests[] = {
    {
        .name = "ime = false",
        .init =
            {
                .mem = {[MEM_IF] = 0xFF, [MEM_IE] = 0xFF},
            },
        .want =
            {
                // interrupt not called, NOP executed.
                .cpu = {.pc = 1},
                .mem = {[MEM_IF] = 0xFF, [MEM_IE] = 0xFF},
            },
        .cycles = 1,
    },
    {
        .name = "IE = false",
        .init =
            {
                .cpu = {.ime = true},
                .mem = {[MEM_IF] = 1, [MEM_IE] = 0},
            },
        .want =
            {
                // interrupt not called, NOP executed.
                .cpu = {.pc = 1, .ime = true},
                .mem = {[MEM_IF] = 1, [MEM_IE] = 0},
            },
        .cycles = 1,
    },
    {
        .name = "call interrupt 0",
        .init =
            {
                .cpu = {.pc = 0x050A, .sp = HIGH_RAM_END, .ime = true},
                .mem =
                    {
                        [0x40] = 7,
                        [MEM_IF] = 1 << 0,
                        [MEM_IE] = 0xFF,
                    },
            },
        .want =
            {
                // interrupt not called, NOP executed.
                .cpu =
                    {
                        .ir = 7,
                        .pc = 0x41,
                        .sp = HIGH_RAM_END - 2,
                        .ime = false,
                    },
                .mem =
                    {
                        [HIGH_RAM_END - 2] = 0x9,
                        [HIGH_RAM_END - 1] = 0x5,
                        [0x40] = 7,
                        [MEM_IF] = 0,
                        [MEM_IE] = 0xFF,
                    },
            },
        .cycles = 5,
    },
    {
        .name = "call interrupt 1",
        .init =
            {
                .cpu = {.pc = 0x050A, .sp = HIGH_RAM_END, .ime = true},
                .mem =
                    {
                        [0x48] = 7,
                        [MEM_IF] = 1 << 1,
                        [MEM_IE] = 0xFF,
                    },
            },
        .want =
            {
                // interrupt not called, NOP executed.
                .cpu =
                    {
                        .ir = 7,
                        .pc = 0x49,
                        .sp = HIGH_RAM_END - 2,
                        .ime = false,
                    },
                .mem =
                    {
                        [HIGH_RAM_END - 2] = 0x9,
                        [HIGH_RAM_END - 1] = 0x5,
                        [0x48] = 7,
                        [MEM_IF] = 0,
                        [MEM_IE] = 0xFF,
                    },
            },
        .cycles = 5,
    },
    {
        .name = "call interrupt 2",
        .init =
            {
                .cpu = {.pc = 0x050A, .sp = HIGH_RAM_END, .ime = true},
                .mem =
                    {
                        [0x50] = 7,
                        [MEM_IF] = 1 << 2,
                        [MEM_IE] = 0xFF,
                    },
            },
        .want =
            {
                // interrupt not called, NOP executed.
                .cpu =
                    {.ir = 7, .pc = 0x51, .sp = HIGH_RAM_END - 2, .ime = false},
                .mem =
                    {
                        [HIGH_RAM_END - 2] = 0x9,
                        [HIGH_RAM_END - 1] = 0x5,
                        [0x50] = 7,
                        [MEM_IF] = 0,
                        [MEM_IE] = 0xFF,
                    },
            },
        .cycles = 5,
    },
    {
        .name = "call interrupt 3",
        .init =
            {
                .cpu = {.pc = 0x050A, .sp = HIGH_RAM_END, .ime = true},
                .mem =
                    {
                        [0x58] = 7,
                        [MEM_IF] = 1 << 3,
                        [MEM_IE] = 0xFF,
                    },
            },
        .want =
            {
                // interrupt not called, NOP executed.
                .cpu =
                    {.ir = 7, .pc = 0x59, .sp = HIGH_RAM_END - 2, .ime = false},
                .mem =
                    {
                        [HIGH_RAM_END - 2] = 0x9,
                        [HIGH_RAM_END - 1] = 0x5,
                        [0x58] = 7,
                        [MEM_IF] = 0,
                        [MEM_IE] = 0xFF,
                    },
            },
        .cycles = 5,
    },
    {
        .name = "call interrupt 4",
        .init =
            {
                .cpu = {.pc = 0x050A, .sp = HIGH_RAM_END, .ime = true},
                .mem =
                    {
                        [0x60] = 7,
                        [MEM_IF] = 1 << 4,
                        [MEM_IE] = 0xFF,
                    },
            },
        .want =
            {
                // interrupt not called, NOP executed.
                .cpu =
                    {.ir = 7, .pc = 0x61, .sp = HIGH_RAM_END - 2, .ime = false},
                .mem =
                    {
                        [HIGH_RAM_END - 2] = 0x9,
                        [HIGH_RAM_END - 1] = 0x5,
                        [0x60] = 7,
                        [MEM_IF] = 0,
                        [MEM_IE] = 0xFF,
                    },
            },
        .cycles = 5,
    },
};

void run_call_interrupt_tests() {
  _run_exec_tests(call_interrupt_tests, ARRAY_SIZE(call_interrupt_tests));
}

void run_call_interrupt_and_reti_test() {
  Gameboy g = {
      .cpu =
          {
              .pc = 0x0A06,
              .ir = INCA,
              .sp = HIGH_RAM_END,
              .ime = true,
          },
      .mem =
          {
              [0x40] = RETI,
              [0x48] = RETI,
              [0x0A05] = INCA,
              [MEM_IF] = 3,
              [MEM_IE] = 0xFF,
          },
  };

  step(&g);

  Gameboy want_interrupt = {
      .cpu =
          {
              .pc = 0x41,
              .ir = RETI,
              .sp = HIGH_RAM_END - 2,
              .ime = false,
              .state = DONE,
          },
      .mem =
          {
              [0x40] = RETI,
              [0x48] = RETI,
              [HIGH_RAM_END - 2] = 0x05,
              [HIGH_RAM_END - 1] = 0x0A,
              [0x0A05] = INCA,
              [MEM_IF] = 2,
              [MEM_IE] = 0xFF,
          },
  };
  char *diff = gameboy_diff(&g, &want_interrupt);
  if (diff != NULL) {
    FAIL("Unexpected interrupt state:\n%s", diff);
  }

  // Should RETI to the INCA.
  step(&g);

  Gameboy want_after_reti = {
      .cpu =
          {
              .pc = 0x0A06,
              .ir = INCA,
              .sp = HIGH_RAM_END,
              .ime = true,
              .state = DONE,
          },
      .mem =
          {
              [0x40] = RETI,
              [0x48] = RETI,
              [HIGH_RAM_END - 2] = 0x05,
              [HIGH_RAM_END - 1] = 0x0A,
              [0x0A05] = INCA,
              [MEM_IF] = 2,
              [MEM_IE] = 0xFF,
          },
  };
  diff = gameboy_diff(&g, &want_after_reti);
  if (diff != NULL) {
    FAIL("Unexpected state after reti:\n%s", diff);
  }

  // Now the next interrupt.
  step(&g);

  Gameboy want_second_interrupt = {
      .cpu =
          {
              .pc = 0x49,
              .ir = RETI,
              .sp = HIGH_RAM_END - 2,
              .ime = false,
          },
      .mem =
          {
              [0x40] = RETI,
              [0x48] = RETI,
              [HIGH_RAM_END - 2] = 0x05,
              [HIGH_RAM_END - 1] = 0x0A,
              [0x0A05] = INCA,
              [MEM_IF] = 0,
              [MEM_IE] = 0xFF,
          },
  };
  diff = gameboy_diff(&g, &want_second_interrupt);
  if (diff != NULL) {
    FAIL("Unexpected state after second interrupt:\n%s", diff);
  }

  // RETI again
  step(&g);

  Gameboy want_second_reti = {
      .cpu =
          {
              .pc = 0x0A06,
              .ir = INCA,
              .sp = HIGH_RAM_END,
              .ime = true,
          },
      .mem =
          {
              [0x40] = RETI,
              [0x48] = RETI,
              [HIGH_RAM_END - 2] = 0x05,
              [HIGH_RAM_END - 1] = 0x0A,
              [0x0A05] = INCA,
              [MEM_IF] = 0,
              [MEM_IE] = 0xFF,
          },
  };
  diff = gameboy_diff(&g, &want_second_reti);
  if (diff != NULL) {
    FAIL("Unexpected state after second reti:\n%s", diff);
  }

  // Should INCA.
  step(&g);

  Gameboy want_after_inca = {
      .cpu =
          {
              .pc = 0x0A07,
              .ir = 0,
              .sp = HIGH_RAM_END,
              .ime = true,
              .state = DONE,
              .registers = {[REG_A] = 1},
          },
      .mem =
          {
              [0x40] = RETI,
              [0x48] = RETI,
              [HIGH_RAM_END - 2] = 0x05,
              [HIGH_RAM_END - 1] = 0x0A,
              [0x0A05] = INCA,
              [MEM_IF] = 0,
              [MEM_IE] = 0xFF,
          },
  };
  diff = gameboy_diff(&g, &want_after_inca);
  if (diff != NULL) {
    FAIL("Unexpected state after inca:\n%s", diff);
  }
}

void run_halt_stays_halted_test() {
  Gameboy g = {
      .cpu =
          {
              .pc = 1,
              .ir = HALT,
              .sp = HIGH_RAM_END,
              .ime = false,
          },
      .mem =
          {
              [0] = HALT,
              [1] = INCA,
              [MEM_IF] = 0,
              [MEM_IE] = 0xFF,
          },
  };

  // Should stay halted, so long as there are no pending interrupts.

  for (int i = 0; i < 10; i++) {
    step(&g);
    Gameboy want_halted = {
        .cpu =
            {
                .pc = 1,
                .ir = INCA,
                .sp = HIGH_RAM_END,
                .ime = false,
                // No interrupts, so stay halted.
                .state = HALTED,
            },
        .mem =
            {
                [0] = HALT,
                [1] = INCA,
                [MEM_IF] = 0,
                [MEM_IE] = 0xFF,
            },
    };
    char *diff = gameboy_diff(&g, &want_halted);
    if (diff != NULL) {
      FAIL("Unexpected halted state count %d:\n%s", i, diff);
    }
  }

  // Now wake up and execute the NOP to reestablish IR and PC.
  g.mem[MEM_IF] = 1;
  step(&g);

  Gameboy want_awake = {
      .cpu =
          {
              .pc = 2,
              .ir = INCA,
              .sp = HIGH_RAM_END,
              .ime = false,
          },
      .mem =
          {
              [0] = HALT,
              [1] = INCA,
              [MEM_IF] = 1,
              [MEM_IE] = 0xFF,
          },
  };
  char *diff = gameboy_diff(&g, &want_awake);
  if (diff != NULL) {
    FAIL("Unexpected awake state:\n%s", diff);
  }
}

void run_halt_ime_false_pending_false_test() {
  Gameboy g = {
      .cpu =
          {
              .pc = 0,
              .ir = HALT,
              .sp = HIGH_RAM_END,
              .ime = false,
          },
      .mem =
          {
              [0] = INCA,
              [MEM_IF] = 0,
              [MEM_IE] = 0xFF,
          },
  };

  step(&g);

  Gameboy want_halted = {
      .cpu =
          {
              .pc = 0,
              .ir = INCA,
              .sp = HIGH_RAM_END,
              .ime = false,
              .state = HALTED,
          },
      .mem =
          {
              [0] = INCA,
              [MEM_IF] = 0,
              [MEM_IE] = 0xFF,
          },
  };
  char *diff = gameboy_diff(&g, &want_halted);
  if (diff != NULL) {
    FAIL("Unexpected halted state:\n%s", diff);
  }

  // Wake up.
  g.mem[MEM_IF] = 1;
  step(&g); // should execute a NOP and re-fetch IR=INCA

  Gameboy want_awake = {
      .cpu =
          {
              .pc = 1,
              .ir = INCA,
              .sp = HIGH_RAM_END,
              .ime = false,
              .state = DONE,
          },
      .mem =
          {
              [0] = INCA,
              [MEM_IF] = 1,
              [MEM_IE] = 0xFF,
          },
  };
  diff = gameboy_diff(&g, &want_awake);
  if (diff != NULL) {
    FAIL("Unexpected awake state:\n%s", diff);
  }
}

void run_halt_ime_false_pending_true_test() {
  Gameboy g = {
      .cpu =
          {
              .pc = 0,
              .ir = HALT,
              .sp = HIGH_RAM_END,
              .ime = false,
          },
      .mem =
          {
              [0] = INCA,
              [MEM_IF] = 1 << 4,
              [MEM_IE] = 0xFF,
          },
  };

  step(&g);

  Gameboy want = {
      .cpu =
          {
              // We never halt in this situation, but instead, we immediately
              // wake up. PC was never incremented, IR is set to INCA, but PC
              // still points to INCA. We will read INCA twice.
              //
              // This is "the HALT bug".
              .pc = 0,
              .ir = INCA,
              .sp = HIGH_RAM_END,
              .ime = false,
              .state = DONE,
          },
      .mem =
          {
              [0] = INCA,
              [MEM_IF] = 1 << 4,
              [MEM_IE] = 0xFF,
          },
  };
  char *diff = gameboy_diff(&g, &want);
  if (diff != NULL) {
    FAIL("Unexpected end state:\n%s", diff);
  }

  step(&g);

  Gameboy want_inca_1 = {
      .cpu =
          {
              .pc = 1,
              .ir = INCA,
              .sp = HIGH_RAM_END,
              .ime = false,
              .state = DONE,
              .registers = {[REG_A] = 1},
          },
      .mem =
          {
              [0] = INCA,
              [MEM_IF] = 1 << 4,
              [MEM_IE] = 0xFF,
          },
  };
  diff = gameboy_diff(&g, &want_inca_1);
  if (diff != NULL) {
    FAIL("Unexpected end state:\n%s", diff);
  }

  step(&g);

  Gameboy want_inca_2 = {
      .cpu =
          {
              .pc = 2,
              .ir = 0,
              .sp = HIGH_RAM_END,
              .ime = false,
              .state = DONE,
              .registers = {[REG_A] = 2},
          },
      .mem =
          {
              [0] = INCA,
              [MEM_IF] = 1 << 4,
              [MEM_IE] = 0xFF,
          },
  };
  diff = gameboy_diff(&g, &want_inca_2);
  if (diff != NULL) {
    FAIL("Unexpected end state:\n%s", diff);
  }
}

void run_halt_after_ei_ime_false_pending_true_test() {
  Gameboy g = {
      .cpu =
          {
              .pc = 0x0A05,
              .ir = EI,
              .sp = HIGH_RAM_END,
              .ime = false,
          },
      .mem =
          {
              [0x40] = RETI,
              [0x0A04] = EI,
              [0x0A05] = HALT,
              [MEM_IF] = 1,
              [MEM_IE] = 0xFF,
          },
  };

  step(&g);

  Gameboy want_after_ei = {
      .cpu =
          {
              .pc = 0x0A06,
              .ir = HALT,
              .sp = HIGH_RAM_END,
              .ime = false,
              .ei_pend = true,
          },
      .mem =
          {
              [0x40] = RETI,
              [0x0A04] = EI,
              [0x0A05] = HALT,
              [MEM_IF] = 1,
              [MEM_IE] = 0xFF,
          },
  };
  char *diff = gameboy_diff(&g, &want_after_ei);
  if (diff != NULL) {
    FAIL("Unexpected state after EI:\n%s", diff);
  }

  step(&g);

  Gameboy want_after_halt = {
      .cpu =
          {
              .pc = 0x0A06,
              .ir = 0, // 0 after HALT; doesn't matter what it is.
              .sp = HIGH_RAM_END,
              .ime = true,
          },
      .mem =
          {
              [0x40] = RETI,
              [0x0A04] = EI,
              [0x0A05] = HALT,
              [MEM_IF] = 1,
              [MEM_IE] = 0xFF,
          },
  };
  diff = gameboy_diff(&g, &want_after_halt);
  if (diff != NULL) {
    FAIL("Unexpected state after HALT:\n%s", diff);
  }

  step(&g);

  Gameboy want_after_interrupt = {
      .cpu =
          {
              .pc = 0x41,
              .ir = RETI,
              .sp = HIGH_RAM_END - 2,
              .ime = false,
          },
      .mem =
          {
              [0x40] = RETI,
              [HIGH_RAM_END - 2] = 0x05,
              [HIGH_RAM_END - 1] = 0x0A,
              [0x0A04] = EI,
              [0x0A05] = HALT,
              [MEM_IF] = 0,
              [MEM_IE] = 0xFF,
          },
  };
  diff = gameboy_diff(&g, &want_after_interrupt);
  if (diff != NULL) {
    FAIL("Unexpected state after interrupt:\n%s", diff);
  }

  step(&g);

  Gameboy want_after_reti = {
      .cpu =
          {
              .pc = 0x0A06,
              .ir = HALT,
              .sp = HIGH_RAM_END,
              .ime = true,
          },
      .mem =
          {
              [0x40] = RETI,
              [HIGH_RAM_END - 2] = 0x05,
              [HIGH_RAM_END - 1] = 0x0A,
              [0x0A04] = EI,
              [0x0A05] = HALT,
              [MEM_IF] = 0,
              [MEM_IE] = 0xFF,
          },
  };
  diff = gameboy_diff(&g, &want_after_reti);
  if (diff != NULL) {
    FAIL("Unexpected state after reti:\n%s", diff);
  }

  step(&g);

  Gameboy want_second_halt = {
      .cpu =
          {
              .pc = 0x0A06,
              .ir = 0,
              .sp = HIGH_RAM_END,
              .ime = true,
              .state = HALTED,
          },
      .mem =
          {
              [0x40] = RETI,
              [HIGH_RAM_END - 2] = 0x05,
              [HIGH_RAM_END - 1] = 0x0A,
              [0x0A04] = EI,
              [0x0A05] = HALT,
              [MEM_IF] = 0,
              [MEM_IE] = 0xFF,
          },
  };
  diff = gameboy_diff(&g, &want_second_halt);
  if (diff != NULL) {
    FAIL("Unexpected state after second HALT:\n%s", diff);
  }
}

void run_halt_then_rst_ime_false_pending_true_test() {
  Gameboy g = {
      .cpu =
          {
              .pc = 0x0A06,
              .ir = HALT,
              .sp = HIGH_RAM_END,
              .ime = false,
          },
      .mem =
          {
              [0] = RET,
              [0x0A05] = HALT,
              [0x0A06] = RST0,
              [MEM_IF] = 1,
              [MEM_IE] = 0xFF,
          },
  };

  step(&g);

  Gameboy want_after_halt = {
      .cpu =
          {
              .pc = 0x0A06,
              .ir = RST0,
              .sp = HIGH_RAM_END,
              .ime = false,
          },
      .mem =
          {
              [0] = RET,
              [0x0A05] = HALT,
              [0x0A06] = RST0,
              [MEM_IF] = 1,
              [MEM_IE] = 0xFF,
          },
  };
  char *diff = gameboy_diff(&g, &want_after_halt);
  if (diff != NULL) {
    FAIL("Unexpected state after HALT:\n%s", diff);
  }

  step(&g);

  Gameboy want_after_rst = {
      .cpu =
          {
              .pc = 1,
              .ir = RET,
              .sp = HIGH_RAM_END - 2,
              .ime = false,
          },
      .mem =
          {
              [0] = RET,
              [0x0A05] = HALT,
              [0x0A06] = RST0,
              [HIGH_RAM_END - 2] = 0x06,
              [HIGH_RAM_END - 1] = 0xA,
              [MEM_IF] = 1,
              [MEM_IE] = 0xFF,
          },
  };
  diff = gameboy_diff(&g, &want_after_rst);
  if (diff != NULL) {
    FAIL("Unexpected state after RST:\n%s", diff);
  }

  step(&g);

  Gameboy want_after_ret = {
      .cpu =
          {
              .pc = 0x0A07,
              .ir = RST0,
              .sp = HIGH_RAM_END,
              .ime = false,
          },
      .mem =
          {
              [0] = RET,
              [0x0A05] = HALT,
              [0x0A06] = RST0,
              [HIGH_RAM_END - 2] = 0x06,
              [HIGH_RAM_END - 1] = 0xA,
              [MEM_IF] = 1,
              [MEM_IE] = 0xFF,
          },
  };
  diff = gameboy_diff(&g, &want_after_ret);
  if (diff != NULL) {
    FAIL("Unexpected state after RET:\n%s", diff);
  }

  step(&g);

  Gameboy want_after_rst_again = {
      .cpu =
          {
              .pc = 1,
              .ir = RET,
              .sp = HIGH_RAM_END - 2,
              .ime = false,
          },
      .mem =
          {
              [0] = RET,
              [0x0A05] = HALT,
              [0x0A06] = RST0,
              [HIGH_RAM_END - 2] = 0x07,
              [HIGH_RAM_END - 1] = 0xA,
              [MEM_IF] = 1,
              [MEM_IE] = 0xFF,
          },
  };
  diff = gameboy_diff(&g, &want_after_rst_again);
  if (diff != NULL) {
    FAIL("Unexpected state RST again:\n%s", diff);
  }
}

void run_halt_ime_true_pending_false_test() {
  Gameboy g = {
      .cpu =
          {
              .pc = 0x0A05,
              .ir = HALT,
              .sp = HIGH_RAM_END,
              .ime = true,
          },
      .mem =
          {
              [0x40] = RETI,
              [0x0A04] = HALT,
              [0x0A05] = INCA,
              [MEM_IF] = 0,
              [MEM_IE] = 0xFF,
          },
  };

  step(&g);

  Gameboy want_halted = {
      .cpu =
          {
              .pc = 0x0A05,
              .ir = INCA,
              .sp = HIGH_RAM_END,
              .ime = true,
              .state = HALTED,
          },
      .mem =
          {
              [0x40] = RETI,
              [0x0A04] = HALT,
              [0x0A05] = INCA,
              [MEM_IF] = 0,
              [MEM_IE] = 0xFF,
          },
  };
  char *diff = gameboy_diff(&g, &want_halted);
  if (diff != NULL) {
    FAIL("Unexpected state after HALT:\n%s", diff);
  }

  // Wake up.
  g.mem[MEM_IF] = 1;
  step(&g); // should execute a NOP and re-fetch IR=INCA

  Gameboy want_awake = {
      .cpu =
          {
              .pc = 0x0A06,
              .ir = INCA,
              .sp = HIGH_RAM_END,
              .ime = true,
              .state = DONE,
          },
      .mem =
          {
              [0x40] = RETI,
              [0x0A04] = HALT,
              [0x0A05] = INCA,
              [MEM_IF] = 1,
              [MEM_IE] = 0xFF,
          },
  };
  diff = gameboy_diff(&g, &want_awake);
  if (diff != NULL) {
    FAIL("Unexpected state after wake up:\n%s", diff);
  }

  // After the NOP, we should call the interrupt.
  step(&g);

  Gameboy want_awake2 = {
      .cpu =
          {
              .pc = 0x41,
              .ir = RETI,
              .sp = HIGH_RAM_END - 2,
              .ime = false,
              .state = DONE,
          },
      .mem =
          {
              [0x40] = RETI,
              // In this case, the NOP fetched, so the return address of the
              // interrupt should be INCA.
              [HIGH_RAM_END - 2] = 0x05,
              [HIGH_RAM_END - 1] = 0x0A,
              [0x0A04] = HALT,
              [0x0A05] = INCA,
              [MEM_IF] = 0,
              [MEM_IE] = 0xFF,
          },
  };
  diff = gameboy_diff(&g, &want_awake2);
  if (diff != NULL) {
    FAIL("Unexpected state after wake up 2:\n%s", diff);
  }
}

void run_halt_ime_true_pending_true_test() {
  Gameboy g = {
      .cpu =
          {
              .pc = 0x0A05,
              .ir = HALT,
              .sp = HIGH_RAM_END,
              .ime = true,
          },
      .mem =
          {
              [0x40] = RETI,
              [0x0A04] = HALT,
              [0x0A05] = INCA,
              [MEM_IF] = 1,
              [MEM_IE] = 0xFF,
          },
  };

  // We should never HALT. Instead, we call the interrupt, and the return
  // address points to the HALT instruction.
  step(&g); // should call the interrupt.

  Gameboy want_awake = {
      .cpu =
          {
              .pc = 0x41,
              .ir = RETI,
              .sp = HIGH_RAM_END - 2,
              .ime = false,
              .state = DONE,
          },
      .mem =
          {
              [0x40] = RETI,
              [HIGH_RAM_END - 2] = 0x04,
              [HIGH_RAM_END - 1] = 0x0A,
              [0x0A04] = HALT,
              [0x0A05] = INCA,
              [MEM_IF] = 0,
              [MEM_IE] = 0xFF,
          },
  };
  char *diff = gameboy_diff(&g, &want_awake);
  if (diff != NULL) {
    FAIL("Unexpected state after wake up:\n%s", diff);
  }

  // Should RETI to the HALT.
  step(&g);

  Gameboy want_awake2 = {
      .cpu =
          {
              .pc = 0x0A05,
              .ir = HALT,
              .sp = HIGH_RAM_END,
              .ime = true,
              .state = DONE,
          },
      .mem =
          {
              [0x40] = RETI,
              [HIGH_RAM_END - 2] = 0x04,
              [HIGH_RAM_END - 1] = 0x0A,
              [0x0A04] = HALT,
              [0x0A05] = INCA,
              [MEM_IF] = 0,
              [MEM_IE] = 0xFF,
          },
  };
  diff = gameboy_diff(&g, &want_awake2);
  if (diff != NULL) {
    FAIL("Unexpected state after wake up 2:\n%s", diff);
  }
}

static struct exec_test store_fetch_tests[] = {
    {
        .name = "Fetch ROM",
        .init =
            {
                .cpu = {.ir = LD_A_IMM16_MEM},
                .mem = {0, 5, [0x0500] = 0xAA},
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0xAA}},
                .mem = {0, 5, [0x0500] = 0xAA},
            },
        .cycles = 4,
    },
    {
        .name = "Store ROM ignored",
        .init =
            {
                .cpu = {.ir = LD_IMM16_MEM_A, .registers = {[REG_A] = 0xAA}},
                .mem = {0, 5},
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0xAA}},
                .mem = {0, 5},
            },
        .cycles = 4,
    },
    {
        .name = "Store ROM ignored",
        .init =
            {
                .cpu = {.ir = LD_IMM16_MEM_A, .registers = {[REG_A] = 0xAA}},
                .mem = {0, 5},
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0xAA}},
                .mem = {0, 5},
            },
        .cycles = 4,
    },
    {
        .name = "Fetch VRAM in mode 0 OK",
        .init =
            {
                .cpu = {.ir = LD_A_IMM16_MEM},
                .mem =
                    {
                        MEM_VRAM_START & 0xFF,
                        MEM_VRAM_START >> 8,
                        [MEM_VRAM_START] = 0xAA,
                        [MEM_STAT] = 0,
                        [MEM_LCDC] = LCDC_ENABLED,
                    },
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0xAA}},
                .mem =
                    {
                        MEM_VRAM_START & 0xFF,
                        MEM_VRAM_START >> 8,
                        [MEM_VRAM_START] = 0xAA,
                        [MEM_STAT] = 0,
                        [MEM_LCDC] = LCDC_ENABLED,
                    },
            },
        .cycles = 4,
    },
    {
        .name = "Store VRAM in mode 0 OK",
        .init =
            {
                .cpu = {.ir = LD_IMM16_MEM_A, .registers = {[REG_A] = 0xAA}},
                .mem =
                    {
                        MEM_VRAM_START & 0xFF,
                        MEM_VRAM_START >> 8,
                        [MEM_STAT] = 0,
                        [MEM_LCDC] = LCDC_ENABLED,
                    },
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0xAA}},
                .mem =
                    {
                        MEM_VRAM_START & 0xFF,
                        MEM_VRAM_START >> 8,
                        [MEM_VRAM_START] = 0xAA,
                        [MEM_STAT] = 0,
                        [MEM_LCDC] = LCDC_ENABLED,
                    },
            },
        .cycles = 4,
    },
    {
        .name = "Fetch VRAM in mode 1 OK",
        .init =
            {
                .cpu = {.ir = LD_A_IMM16_MEM},
                .mem =
                    {
                        MEM_VRAM_START & 0xFF,
                        MEM_VRAM_START >> 8,
                        [MEM_VRAM_START] = 0xAA,
                        [MEM_STAT] = 1,
                        [MEM_LCDC] = LCDC_ENABLED,
                    },
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0xAA}},
                .mem =
                    {
                        MEM_VRAM_START & 0xFF,
                        MEM_VRAM_START >> 8,
                        [MEM_VRAM_START] = 0xAA,
                        [MEM_STAT] = 1,
                        [MEM_LCDC] = LCDC_ENABLED,
                    },
            },
        .cycles = 4,
    },
    {
        .name = "Store VRAM in mode 1 OK",
        .init =
            {
                .cpu = {.ir = LD_IMM16_MEM_A, .registers = {[REG_A] = 0xAA}},
                .mem =
                    {
                        MEM_VRAM_START & 0xFF,
                        MEM_VRAM_START >> 8,
                        [MEM_STAT] = 1,
                        [MEM_LCDC] = LCDC_ENABLED,
                    },
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0xAA}},
                .mem =
                    {
                        MEM_VRAM_START & 0xFF,
                        MEM_VRAM_START >> 8,
                        [MEM_VRAM_START] = 0xAA,
                        [MEM_STAT] = 1,
                        [MEM_LCDC] = LCDC_ENABLED,
                    },
            },
        .cycles = 4,
    },
    {
        .name = "Fetch VRAM in mode 2 OK",
        .init =
            {
                .cpu = {.ir = LD_A_IMM16_MEM},
                .mem =
                    {
                        MEM_VRAM_START & 0xFF,
                        MEM_VRAM_START >> 8,
                        [MEM_VRAM_START] = 0xAA,
                        [MEM_STAT] = 2,
                        [MEM_LCDC] = LCDC_ENABLED,
                    },
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0xAA}},
                .mem =
                    {
                        MEM_VRAM_START & 0xFF,
                        MEM_VRAM_START >> 8,
                        [MEM_VRAM_START] = 0xAA,
                        [MEM_STAT] = 2,
                        [MEM_LCDC] = LCDC_ENABLED,
                    },
            },
        .cycles = 4,
    },
    {
        .name = "Store VRAM in mode 2 OK",
        .init =
            {
                .cpu = {.ir = LD_IMM16_MEM_A, .registers = {[REG_A] = 0xAA}},
                .mem =
                    {
                        MEM_VRAM_START & 0xFF,
                        MEM_VRAM_START >> 8,
                        [MEM_STAT] = 2,
                        [MEM_LCDC] = LCDC_ENABLED,
                    },
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0xAA}},
                .mem =
                    {
                        MEM_VRAM_START & 0xFF,
                        MEM_VRAM_START >> 8,
                        [MEM_VRAM_START] = 0xAA,
                        [MEM_STAT] = 2,
                        [MEM_LCDC] = LCDC_ENABLED,
                    },
            },
        .cycles = 4,
    },
    {
        .name = "Fetch VRAM in mode 3 ignored",
        .init =
            {
                .cpu = {.ir = LD_A_IMM16_MEM},
                .mem =
                    {
                        MEM_VRAM_START & 0xFF,
                        MEM_VRAM_START >> 8,
                        [MEM_VRAM_START] = 0xAA,
                        [MEM_STAT] = 3,
                        [MEM_LCDC] = LCDC_ENABLED,
                    },
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0xFF}},
                .mem =
                    {
                        MEM_VRAM_START & 0xFF,
                        MEM_VRAM_START >> 8,
                        [MEM_VRAM_START] = 0xAA,
                        [MEM_STAT] = 3,
                        [MEM_LCDC] = LCDC_ENABLED,
                    },
            },
        .cycles = 4,
    },
    {
        .name = "Store VRAM in mode 3 ignored",
        .init =
            {
                .cpu = {.ir = LD_IMM16_MEM_A, .registers = {[REG_A] = 0xAA}},
                .mem =
                    {
                        MEM_VRAM_START & 0xFF,
                        MEM_VRAM_START >> 8,
                        [MEM_STAT] = 3,
                        [MEM_LCDC] = LCDC_ENABLED,
                    },
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0xAA}},
                .mem =
                    {
                        MEM_VRAM_START & 0xFF,
                        MEM_VRAM_START >> 8,
                        [MEM_STAT] = 3,
                        [MEM_LCDC] = LCDC_ENABLED,
                    },
            },
        .cycles = 4,
    },
    {
        .name = "Fetch OAM in mode 0 OK",
        .init =
            {
                .cpu = {.ir = LD_A_IMM16_MEM},
                .mem =
                    {
                        MEM_OAM_START & 0xFF,
                        MEM_OAM_START >> 8,
                        [MEM_OAM_START] = 0xAA,
                        [MEM_STAT] = 0,
                        [MEM_LCDC] = LCDC_ENABLED,
                    },
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0xAA}},
                .mem =
                    {
                        MEM_OAM_START & 0xFF,
                        MEM_OAM_START >> 8,
                        [MEM_OAM_START] = 0xAA,
                        [MEM_STAT] = 0,
                        [MEM_LCDC] = LCDC_ENABLED,
                    },
            },
        .cycles = 4,
    },
    {
        .name = "Store OAM in mode 0 OK",
        .init =
            {
                .cpu = {.ir = LD_IMM16_MEM_A, .registers = {[REG_A] = 0xAA}},
                .mem =
                    {
                        MEM_OAM_START & 0xFF,
                        MEM_OAM_START >> 8,
                        [MEM_STAT] = 0,
                        [MEM_LCDC] = LCDC_ENABLED,
                    },
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0xAA}},
                .mem =
                    {
                        MEM_OAM_START & 0xFF,
                        MEM_OAM_START >> 8,
                        [MEM_OAM_START] = 0xAA,
                        [MEM_STAT] = 0,
                        [MEM_LCDC] = LCDC_ENABLED,
                    },
            },
        .cycles = 4,
    },
    {
        .name = "Fetch OAM in mode 1 OK",
        .init =
            {
                .cpu = {.ir = LD_A_IMM16_MEM},
                .mem =
                    {
                        MEM_OAM_START & 0xFF,
                        MEM_OAM_START >> 8,
                        [MEM_OAM_START] = 0xAA,
                        [MEM_STAT] = 1,
                        [MEM_LCDC] = LCDC_ENABLED,
                    },
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0xAA}},
                .mem =
                    {
                        MEM_OAM_START & 0xFF,
                        MEM_OAM_START >> 8,
                        [MEM_OAM_START] = 0xAA,
                        [MEM_STAT] = 1,
                        [MEM_LCDC] = LCDC_ENABLED,
                    },
            },
        .cycles = 4,
    },
    {
        .name = "Store OAM in mode 1 OK",
        .init =
            {
                .cpu = {.ir = LD_IMM16_MEM_A, .registers = {[REG_A] = 0xAA}},
                .mem =
                    {
                        MEM_OAM_START & 0xFF,
                        MEM_OAM_START >> 8,
                        [MEM_STAT] = 1,
                        [MEM_LCDC] = LCDC_ENABLED,
                    },
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0xAA}},
                .mem =
                    {
                        MEM_OAM_START & 0xFF,
                        MEM_OAM_START >> 8,
                        [MEM_OAM_START] = 0xAA,
                        [MEM_STAT] = 1,
                        [MEM_LCDC] = LCDC_ENABLED,
                    },
            },
        .cycles = 4,
    },
    {
        .name = "Fetch OAM in mode 2 ignored",
        .init =
            {
                .cpu = {.ir = LD_A_IMM16_MEM},
                .mem =
                    {
                        MEM_OAM_START & 0xFF,
                        MEM_OAM_START >> 8,
                        [MEM_OAM_START] = 0xAA,
                        [MEM_STAT] = 2,
                        [MEM_LCDC] = LCDC_ENABLED,
                    },
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0xFF}},
                .mem =
                    {
                        MEM_OAM_START & 0xFF,
                        MEM_OAM_START >> 8,
                        [MEM_OAM_START] = 0xAA,
                        [MEM_STAT] = 2,
                        [MEM_LCDC] = LCDC_ENABLED,
                    },
            },
        .cycles = 4,
    },
    {
        .name = "Store OAM in mode 2 ignored",
        .init =
            {
                .cpu = {.ir = LD_IMM16_MEM_A, .registers = {[REG_A] = 0xAA}},
                .mem =
                    {
                        MEM_OAM_START & 0xFF,
                        MEM_OAM_START >> 8,
                        [MEM_STAT] = 2,
                        [MEM_LCDC] = LCDC_ENABLED,
                    },
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0xAA}},
                .mem =
                    {
                        MEM_OAM_START & 0xFF,
                        MEM_OAM_START >> 8,
                        [MEM_STAT] = 2,
                        [MEM_LCDC] = LCDC_ENABLED,
                    },
            },
        .cycles = 4,
    },
    {
        .name = "Fetch OAM in mode 2 OK when PPU is off",
        .init =
            {
                .cpu = {.ir = LD_A_IMM16_MEM},
                .mem =
                    {
                        MEM_OAM_START & 0xFF,
                        MEM_OAM_START >> 8,
                        [MEM_OAM_START] = 0xAA,
                        [MEM_STAT] = 2,
                        [MEM_LCDC] = 0,
                    },
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0xAA}},
                .mem =
                    {
                        MEM_OAM_START & 0xFF,
                        MEM_OAM_START >> 8,
                        [MEM_OAM_START] = 0xAA,
                        [MEM_STAT] = 2,
                        [MEM_LCDC] = 0,
                    },
            },
        .cycles = 4,
    },
    {
        .name = "Store OAM in mode 2 OK when PPU is off",
        .init =
            {
                .cpu = {.ir = LD_IMM16_MEM_A, .registers = {[REG_A] = 0xAA}},
                .mem =
                    {
                        MEM_OAM_START & 0xFF,
                        MEM_OAM_START >> 8,
                        [MEM_STAT] = 2,
                        [MEM_LCDC] = 0,
                    },
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0xAA}},
                .mem =
                    {
                        MEM_OAM_START & 0xFF,
                        MEM_OAM_START >> 8,
                        [MEM_OAM_START] = 0xAA,
                        [MEM_STAT] = 2,
                        [MEM_LCDC] = 0,
                    },
            },
        .cycles = 4,
    },
    {
        .name = "Fetch OAM in mode 3 ignored",
        .init =
            {
                .cpu = {.ir = LD_A_IMM16_MEM},
                .mem =
                    {
                        MEM_OAM_START & 0xFF,
                        MEM_OAM_START >> 8,
                        [MEM_OAM_START] = 0xAA,
                        [MEM_STAT] = 3,
                        [MEM_LCDC] = LCDC_ENABLED,
                    },
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0xFF}},
                .mem =
                    {
                        MEM_OAM_START & 0xFF,
                        MEM_OAM_START >> 8,
                        [MEM_OAM_START] = 0xAA,
                        [MEM_STAT] = 3,
                        [MEM_LCDC] = LCDC_ENABLED,
                    },
            },
        .cycles = 4,
    },
    {
        .name = "Store OAM in mode 3 ignored",
        .init =
            {
                .cpu = {.ir = LD_IMM16_MEM_A, .registers = {[REG_A] = 0xAA}},
                .mem =
                    {
                        MEM_OAM_START & 0xFF,
                        MEM_OAM_START >> 8,
                        [MEM_STAT] = 3,
                        [MEM_LCDC] = LCDC_ENABLED,
                    },
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0xAA}},
                .mem =
                    {
                        MEM_OAM_START & 0xFF,
                        MEM_OAM_START >> 8,
                        [MEM_STAT] = 3,
                        [MEM_LCDC] = LCDC_ENABLED,
                    },
            },
        .cycles = 4,
    },
    {
        // Echo ram is mapped to 0xC000-0xDDFF.
        .name = "Fetch echo RAM",
        .init =
            {
                .cpu = {.ir = LD_A_IMM16_MEM},
                .mem =
                    {
                        MEM_ECHO_RAM_START & 0xFF,
                        MEM_ECHO_RAM_START >> 8,
                        [0xC000] = 0xAA,
                    },
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0xAA}},
                .mem =
                    {
                        MEM_ECHO_RAM_START & 0xFF,
                        MEM_ECHO_RAM_START >> 8,
                        [0xC000] = 0xAA,
                    },
            },
        .cycles = 4,
    },
    {
        // Echo ram is mapped to 0xC000-0xDDFF.
        .name = "Store echo RAM",
        .init =
            {
                .cpu = {.ir = LD_IMM16_MEM_A, .registers = {[REG_A] = 0xAA}},
                .mem =
                    {
                        MEM_ECHO_RAM_START & 0xFF,
                        MEM_ECHO_RAM_START >> 8,
                    },
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0xAA}},
                .mem =
                    {
                        MEM_ECHO_RAM_START & 0xFF,
                        MEM_ECHO_RAM_START >> 8,
                        [0xC000] = 0xAA,
                    },
            },
        .cycles = 4,
    },
    {
        .name = "Store P1/JOYPAD select nothing",
        .init =
            {
                .cpu = {.ir = LD_IMM16_MEM_A, .registers = {[REG_A] = 0x30}},
                .mem =
                    {
                        MEM_P1_JOYPAD & 0xFF,
                        MEM_P1_JOYPAD >> 8,
                    },
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0x30}},
                .mem =
                    {
                        MEM_P1_JOYPAD & 0xFF,
                        MEM_P1_JOYPAD >> 8,
                        [MEM_P1_JOYPAD] = 0x3F,
                    },
            },
        .cycles = 4,
    },
    {
        .name = "Store P1/JOYPAD select dpad RIGHT",
        .init =
            {
                .cpu = {.ir = LD_IMM16_MEM_A, .registers = {[REG_A] = 0x20}},
                .dpad = BUTTON_RIGHT,
                .mem =
                    {
                        MEM_P1_JOYPAD & 0xFF,
                        MEM_P1_JOYPAD >> 8,
                    },
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0x20}},
                .dpad = BUTTON_RIGHT,
                .mem =
                    {
                        MEM_P1_JOYPAD & 0xFF,
                        MEM_P1_JOYPAD >> 8,
                        [MEM_P1_JOYPAD] = 0x20 | (~BUTTON_RIGHT & 0xF),
                    },
            },
        .cycles = 4,
    },
    {
        .name = "Store P1/JOYPAD select dpad LEFT",
        .init =
            {
                .cpu = {.ir = LD_IMM16_MEM_A, .registers = {[REG_A] = 0x20}},
                .dpad = BUTTON_LEFT,
                .mem =
                    {
                        MEM_P1_JOYPAD & 0xFF,
                        MEM_P1_JOYPAD >> 8,
                    },
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0x20}},
                .dpad = BUTTON_LEFT,
                .mem =
                    {
                        MEM_P1_JOYPAD & 0xFF,
                        MEM_P1_JOYPAD >> 8,
                        [MEM_P1_JOYPAD] = 0x20 | (~BUTTON_LEFT & 0xF),
                    },
            },
        .cycles = 4,
    },
    {
        .name = "Store P1/JOYPAD select dpad UP",
        .init =
            {
                .cpu = {.ir = LD_IMM16_MEM_A, .registers = {[REG_A] = 0x20}},
                .dpad = BUTTON_UP,
                .mem =
                    {
                        MEM_P1_JOYPAD & 0xFF,
                        MEM_P1_JOYPAD >> 8,
                    },
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0x20}},
                .dpad = BUTTON_UP,
                .mem =
                    {
                        MEM_P1_JOYPAD & 0xFF,
                        MEM_P1_JOYPAD >> 8,
                        [MEM_P1_JOYPAD] = 0x20 | (~BUTTON_UP & 0xF),
                    },
            },
        .cycles = 4,
    },
    {
        .name = "Store P1/JOYPAD select dpad DOWN",
        .init =
            {
                .cpu = {.ir = LD_IMM16_MEM_A, .registers = {[REG_A] = 0x20}},
                .dpad = BUTTON_DOWN,
                .mem =
                    {
                        MEM_P1_JOYPAD & 0xFF,
                        MEM_P1_JOYPAD >> 8,
                    },
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0x20}},
                .dpad = BUTTON_DOWN,
                .mem =
                    {
                        MEM_P1_JOYPAD & 0xFF,
                        MEM_P1_JOYPAD >> 8,
                        [MEM_P1_JOYPAD] = 0x20 | (~BUTTON_DOWN & 0xF),
                    },
            },
        .cycles = 4,
    },
    {
        .name = "Store P1/JOYPAD select dpad UP and LEFT",
        .init =
            {
                .cpu = {.ir = LD_IMM16_MEM_A, .registers = {[REG_A] = 0x20}},
                .dpad = BUTTON_UP | BUTTON_LEFT,
                .mem =
                    {
                        MEM_P1_JOYPAD & 0xFF,
                        MEM_P1_JOYPAD >> 8,
                    },
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0x20}},
                .dpad = BUTTON_UP | BUTTON_LEFT,
                .mem =
                    {
                        MEM_P1_JOYPAD & 0xFF,
                        MEM_P1_JOYPAD >> 8,
                        [MEM_P1_JOYPAD] =
                            0x20 | (~(BUTTON_UP | BUTTON_LEFT) & 0xF),
                    },
            },
        .cycles = 4,
    },
    {
        .name = "Store P1/JOYPAD select button A",
        .init =
            {
                .cpu = {.ir = LD_IMM16_MEM_A, .registers = {[REG_A] = 0x10}},
                .buttons = BUTTON_A,
                .mem =
                    {
                        MEM_P1_JOYPAD & 0xFF,
                        MEM_P1_JOYPAD >> 8,
                    },
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0x10}},
                .buttons = BUTTON_A,
                .mem =
                    {
                        MEM_P1_JOYPAD & 0xFF,
                        MEM_P1_JOYPAD >> 8,
                        [MEM_P1_JOYPAD] = 0x10 | (~BUTTON_A & 0xF),
                    },
            },
        .cycles = 4,
    },
    {
        .name = "Store P1/JOYPAD select button B",
        .init =
            {
                .cpu = {.ir = LD_IMM16_MEM_A, .registers = {[REG_A] = 0x10}},
                .buttons = BUTTON_B,
                .mem =
                    {
                        MEM_P1_JOYPAD & 0xFF,
                        MEM_P1_JOYPAD >> 8,
                    },
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0x10}},
                .buttons = BUTTON_B,
                .mem =
                    {
                        MEM_P1_JOYPAD & 0xFF,
                        MEM_P1_JOYPAD >> 8,
                        [MEM_P1_JOYPAD] = 0x10 | (~BUTTON_B & 0xF),
                    },
            },
        .cycles = 4,
    },
    {
        .name = "Store P1/JOYPAD select button SELECT",
        .init =
            {
                .cpu = {.ir = LD_IMM16_MEM_A, .registers = {[REG_A] = 0x10}},
                .buttons = BUTTON_SELECT,
                .mem =
                    {
                        MEM_P1_JOYPAD & 0xFF,
                        MEM_P1_JOYPAD >> 8,
                    },
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0x10}},
                .buttons = BUTTON_SELECT,
                .mem =
                    {
                        MEM_P1_JOYPAD & 0xFF,
                        MEM_P1_JOYPAD >> 8,
                        [MEM_P1_JOYPAD] = 0x10 | (~BUTTON_SELECT & 0xF),
                    },
            },
        .cycles = 4,
    },
    {
        .name = "Store P1/JOYPAD select button START",
        .init =
            {
                .cpu = {.ir = LD_IMM16_MEM_A, .registers = {[REG_A] = 0x10}},
                .buttons = BUTTON_START,
                .mem =
                    {
                        MEM_P1_JOYPAD & 0xFF,
                        MEM_P1_JOYPAD >> 8,
                    },
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0x10}},
                .buttons = BUTTON_START,
                .mem =
                    {
                        MEM_P1_JOYPAD & 0xFF,
                        MEM_P1_JOYPAD >> 8,
                        [MEM_P1_JOYPAD] = 0x10 | (~BUTTON_START & 0xF),
                    },
            },
        .cycles = 4,
    },
    {
        .name = "Store P1/JOYPAD select button A and START",
        .init =
            {
                .cpu = {.ir = LD_IMM16_MEM_A, .registers = {[REG_A] = 0x10}},
                .buttons = BUTTON_A | BUTTON_START,
                .mem =
                    {
                        MEM_P1_JOYPAD & 0xFF,
                        MEM_P1_JOYPAD >> 8,
                    },
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0x10}},
                .buttons = BUTTON_A | BUTTON_START,
                .mem =
                    {
                        MEM_P1_JOYPAD & 0xFF,
                        MEM_P1_JOYPAD >> 8,
                        [MEM_P1_JOYPAD] =
                            0x10 | (~(BUTTON_A | BUTTON_START) & 0xF),
                    },
            },
        .cycles = 4,
    },
    {
        .name = "Store P1/JOYPAD select button and dpad",
        .init =
            {
                .cpu = {.ir = LD_IMM16_MEM_A, .registers = {[REG_A] = 0}},
                .buttons = 1 | 4,
                .dpad = 2 | 8,
                .mem =
                    {
                        MEM_P1_JOYPAD & 0xFF,
                        MEM_P1_JOYPAD >> 8,
                    },
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0}},
                .buttons = 1 | 4,
                .dpad = 2 | 8,
                .mem =
                    {
                        MEM_P1_JOYPAD & 0xFF,
                        MEM_P1_JOYPAD >> 8,
                        [MEM_P1_JOYPAD] = 0,
                    },
            },
        .cycles = 4,
    },
    {
        .name = "Store P1/JOYPAD bottom nibble is read-only",
        .init =
            {
                .cpu = {.ir = LD_IMM16_MEM_A, .registers = {[REG_A] = 0x03}},
                .buttons = 0xF,
                .mem =
                    {
                        MEM_P1_JOYPAD & 0xFF,
                        MEM_P1_JOYPAD >> 8,
                    },
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0x03}},
                .buttons = 0xF,
                .mem =
                    {
                        MEM_P1_JOYPAD & 0xFF,
                        MEM_P1_JOYPAD >> 8,
                        [MEM_P1_JOYPAD] = 0,
                    },
            },
        .cycles = 4,
    },
    {
        .name = "Store DIV",
        .init =
            {
                .cpu = {.ir = LD_IMM16_MEM_A, .registers = {[REG_A] = 0xA5}},
                .mem = {MEM_DIV & 0xFF, MEM_DIV >> 8, [MEM_DIV] = 0xF0},
                .counter = 0xF030,
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0xA5}},
                .mem = {MEM_DIV & 0xFF, MEM_DIV >> 8, [MEM_DIV] = 0},
                .counter = 0,
            },
        .cycles = 4,
    },
    {
        .name = "Store STAT (lower 3 bits are read-only)",
        .init =
            {
                .cpu = {.ir = LD_IMM16_MEM_A, .registers = {[REG_A] = 0xFF}},
                .mem =
                    {
                        MEM_STAT & 0xFF,
                        MEM_STAT >> 8,
                        [MEM_STAT] = 0,
                    },
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0xFF}},
                .mem =
                    {
                        MEM_STAT & 0xFF,
                        MEM_STAT >> 8,
                        [MEM_STAT] = 0xF8,
                    },
            },
        .cycles = 4,
    },
    {
        .name = "Store LY (read only)",
        .init =
            {
                .cpu = {.ir = LD_IMM16_MEM_A, .registers = {[REG_A] = 0xA5}},
                .mem = {MEM_LY & 0xFF, MEM_LY >> 8, [MEM_LY] = 10},
            },
        .want =
            {
                .cpu = {.pc = 3, .registers = {[REG_A] = 0xA5}},
                .mem = {MEM_LY & 0xFF, MEM_LY >> 8, [MEM_LY] = 10},
            },
        .cycles = 4,
    },
    {
        .name = "Store OAM DMA ",
        .init =
            {
                .cpu =
                    {
                        // During OAM DMA the CPU can only access high RAM.
                        .pc = HIGH_RAM_START,
                        .ir = LD_IMM16_MEM_A,
                        .registers = {[REG_A] = 10},
                    },
                .mem =
                    {
                        [HIGH_RAM_START] = MEM_DMA & 0xFF,
                        [HIGH_RAM_START + 1] = MEM_DMA >> 8,
                    },
            },
        .want =
            {
                .cpu =
                    {
                        .pc = HIGH_RAM_START + 3,
                        .registers = {[REG_A] = 10},
                    },
                .dma_ticks_remaining = DMA_MCYCLES + DMA_SETUP_MCYCLES,
                .mem =
                    {
                        [HIGH_RAM_START] = MEM_DMA & 0xFF,
                        [HIGH_RAM_START + 1] = MEM_DMA >> 8,
                        [MEM_DMA] = 10,
                    },
            },
        .cycles = 4,
    },
    {
        .name = "Store ignored during OAM DMA",
        .init =
            {
                .cpu =
                    {
                        // During OAM DMA the CPU can only access high RAM.
                        // This includes reading from PC, so let's just point PC
                        // at high RAM.
                        .pc = HIGH_RAM_START,
                        .ir = LD_IMM16_MEM_A,
                        .registers = {[REG_A] = 0xFF},
                    },
                .dma_ticks_remaining = 5,
                .mem =
                    {
                        [HIGH_RAM_START] = MEM_WRAM_START & 0xFF,
                        [HIGH_RAM_START + 1] = MEM_WRAM_START >> 8,
                        [MEM_WRAM_START] = 0xAA,
                    },
            },
        .want =
            {
                .cpu =
                    {
                        .pc = HIGH_RAM_START + 3,
                        .registers = {[REG_A] = 0xFF},
                    },
                .dma_ticks_remaining = 5,
                .mem =
                    {
                        [HIGH_RAM_START] = MEM_WRAM_START & 0xFF,
                        [HIGH_RAM_START + 1] = MEM_WRAM_START >> 8,
                        [MEM_WRAM_START] = 0xAA,
                    },
            },
        .cycles = 4,
    },
    {
        .name = "Fetch ignored during OAM DMA",
        .init =
            {
                .cpu =
                    {
                        // During OAM DMA the CPU can only access high RAM.
                        // This includes reading from PC, so let's just point PC
                        // at high RAM.
                        .pc = HIGH_RAM_START,
                        .ir = LD_A_IMM16_MEM,
                    },
                .dma_ticks_remaining = 5,
                .mem =
                    {
                        [HIGH_RAM_START] = MEM_WRAM_START & 0xFF,
                        [HIGH_RAM_START + 1] = MEM_WRAM_START >> 8,
                        [MEM_WRAM_START] = 0xAA,
                    },
            },
        .want =
            {
                .cpu =
                    {
                        .pc = HIGH_RAM_START + 3,
                        .registers = {[REG_A] = 0xFF},
                    },
                .dma_ticks_remaining = 5,
                .mem =
                    {
                        [HIGH_RAM_START] = MEM_WRAM_START & 0xFF,
                        [HIGH_RAM_START + 1] = MEM_WRAM_START >> 8,
                        [MEM_WRAM_START] = 0xAA,
                    },
            },
        .cycles = 4,
    },
};

void run_store_fetch_tests() {
  _run_exec_tests(store_fetch_tests, ARRAY_SIZE(store_fetch_tests));
}

struct mbc_test {
  const char *name;
  CartType cart_type;
  int num_banks;
  int switch_to_bank;
  int expected_bank;
};

void _run_mbc_tests(struct mbc_test mbc_tests[], int n) {
  for (int i = 0; i < n; i++) {
    struct mbc_test *test = &mbc_tests[i];
    int rom_size = ROM_BANK_SIZE * test->num_banks;
    uint8_t *data = calloc(1, rom_size);
    for (int j = 0; j < test->num_banks; j++) {
      data[ROM_BANK_SIZE * j] = j;
    }
    Rom rom = {
        .data = data,
        .size = rom_size,
        .cart_type = test->cart_type,
        .rom_size = rom_size,
        .num_rom_banks = test->num_banks,
    };
    Gameboy g = {
        .cpu =
            {
                .ir = LD_IMM16_MEM_A,
                .registers = {[REG_A] = test->switch_to_bank},
            },
        .mem =
            {
                // address 0x2000 is MBC1 ROM bank register.
                [0] = 0x00,
                [1] = 0x20,
            },
        .rom = &rom,
    };
    Gameboy want = g;
    want.mem[MEM_ROM_N_START] = test->expected_bank;
    want.cpu.ir = 0;
    want.cpu.pc = 3;

    step(&g);

    char *diff = gameboy_diff(&g, &want);
    if (diff != NULL) {
      FAIL("%s: Unexpected ROM bank switch:\n%s", test->name, diff);
    }

    free(data);
  }
}

struct mbc_test mbc1_tests[] = {
    {
        .name = "Bank 0 is bank 1",
        .cart_type = CART_MBC1,
        .num_banks = 3,
        .switch_to_bank = 0,
        .expected_bank = 1,
    },
    {
        .name = "Switch to bank 1",
        .cart_type = CART_MBC1,
        .num_banks = 3,
        .switch_to_bank = 1,
        .expected_bank = 1,
    },
    {
        .name = "Switch to bank 2",
        .cart_type = CART_MBC1,
        .num_banks = 3,
        .switch_to_bank = 2,
        .expected_bank = 2,
    },
    {
        .name = "Switch to bank 3 wraps",
        .cart_type = CART_MBC1,
        .num_banks = 3,
        .switch_to_bank = 1,
        .expected_bank = 1,
    },
};

void run_mbc1_tests() { _run_mbc_tests(mbc1_tests, ARRAY_SIZE(mbc1_tests)); }

int main() {
  // Turn off fprintf statements for testing storing/fetching VRAM/OAM when it's
  // inaccessible.
  extern bool shhhh;
  shhhh = true;

  run_disassemble_tests();
  run_disassemble_zero_test();
  run_disassemble_instr_too_big_mem_size_1_test();
  run_disassemble_instr_too_big_mem_size_2_test();
  run_disassemble_cb_instr_too_big_test();
  run_disassemble_instr_too_big_mem_size_3_offs_1_test();
  run_cb_disassemble_tests();
  run_reg8_get_set_tests();
  run_reg16_get_set_tests();
  run_exec_tests();
  run_ei_delayed_test();
  run_ei_di_test();
  run_call_interrupt_tests();
  run_call_interrupt_and_reti_test();

  // Test various cases of HALT and interrupts.
  run_halt_stays_halted_test();
  run_halt_ime_false_pending_false_test();
  run_halt_ime_false_pending_true_test();
  run_halt_after_ei_ime_false_pending_true_test();
  run_halt_then_rst_ime_false_pending_true_test();
  run_halt_ime_true_pending_false_test();
  run_halt_ime_true_pending_true_test();

  run_store_fetch_tests();

  run_mbc1_tests();

  return 0;
}
