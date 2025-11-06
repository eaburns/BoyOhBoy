#include "gameboy.h"

#include <stdio.h>
#include <string.h>

struct snprint_test {
  uint8_t op;
  const char *str;
};

// The test tests the opcode followed by bytes 0x01 and 0x02.
// If loaded as imm8, the value is 1.
// If loaded as imm16, the value is 513.
static struct snprint_test snprint_tests[] = {
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
    {.op = 0xC2, .str = "JP NZ, 513 ($0201)"},
    {.op = 0xC3, .str = "JP 513 ($0201)"},
    {.op = 0xC4, .str = "CALL NZ, 513 ($0201)"},
    {.op = 0xC5, .str = "PUSH BC"},
    {.op = 0xC6, .str = "ADD A, 1 ($01)"},
    {.op = 0xC7, .str = "RST 0"},
    {.op = 0xC8, .str = "RET Z"},
    {.op = 0xC9, .str = "RET"},
    {.op = 0xCA, .str = "JP Z, 513 ($0201)"},
    /* 0xCB 0x01 0x02 is CB-prefixed instructions RLC C. */
    {.op = 0xCB, .str = "RLC C"},
    {.op = 0xCC, .str = "CALL Z, 513 ($0201)"},
    {.op = 0xCD, .str = "CALL 513 ($0201)"},
    {.op = 0xCE, .str = "ADC A, 1 ($01)"},
    {.op = 0xCF, .str = "RST 8"},
    {.op = 0xD0, .str = "RET NC"},
    {.op = 0xD1, .str = "POP DE"},
    {.op = 0xD2, .str = "JP NC, 513 ($0201)"},
    {.op = 0xD3, .str = "UNKNOWN"},
    {.op = 0xD4, .str = "CALL NC, 513 ($0201)"},
    {.op = 0xD5, .str = "PUSH DE"},
    {.op = 0xD6, .str = "SUB A, 1 ($01)"},
    {.op = 0xD7, .str = "RST 16"},
    {.op = 0xD8, .str = "RET C"},
    {.op = 0xD9, .str = "RETI"},
    {.op = 0xDA, .str = "JP C, 513 ($0201)"},
    {.op = 0xDB, .str = "UNKNOWN"},
    {.op = 0xDC, .str = "CALL C, 513 ($0201)"},
    {.op = 0xDD, .str = "UNKNOWN"},
    {.op = 0xDE, .str = "SBC A, 1 ($01)"},
    {.op = 0xDF, .str = "RST 24"},
    {.op = 0xE0, .str = "LDH [$FF01], A"},
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
    {.op = 0xF0, .str = "LDH A, [$FF01]"},
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

static struct snprint_test cb_snprint_tests[] = {
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

void run_snprint_tests() {
  for (struct snprint_test *test = snprint_tests;
       test <
       snprint_tests + sizeof(snprint_tests) / sizeof(struct snprint_test);
       test++) {
    Mem mem;
    mem[0] = test->op;
    mem[1] = 0x01;
    mem[2] = 0x02;
    char buf[INSTRUCTION_STR_MAX];
    format_instruction(buf, sizeof(buf), mem, 0);
    if (strcmp(buf, test->str) != 0) {
      fail("op_code: 0x%02x printed as %s, but expected %s", test->op, buf,
           test->str);
    }
  }
}

void run_cb_snprint_tests() {
  for (struct snprint_test *test = cb_snprint_tests;
       test < cb_snprint_tests +
                  sizeof(cb_snprint_tests) / sizeof(struct snprint_test);
       test++) {
    Mem mem;
    mem[0] = 0xCB;
    mem[1] = test->op;
    mem[2] = 0x01;
    mem[3] = 0x02;
    char buf[INSTRUCTION_STR_MAX];
    format_instruction(buf, sizeof(buf), mem, 0);
    if (strcmp(buf, test->str) != 0) {
      fail("op_code: 0x%02x printed as %s, but expected %s", test->op, buf,
           test->str);
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
        fail("set_reg(%s, 1), get_reg(%s)=%d, wanted 1", reg8_name(r),
             reg8_name(r), got);
      }
      if (s != r && got != 0) {
        fail("set_reg(%s, 1), get_reg(%s)=%d, wanted 0", reg8_name(r),
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
      fail("set_reg(BC, 1), get_reg(BC)=0x%04x, wanted 0x0201",
           get_reg16(&cpu, REG_BC));
    }
    if (get_reg8(&cpu, REG_B) != 2) {
      fail("set_reg(BC, 1), get_reg(B)=%d, wanted 2", get_reg8(&cpu, REG_B));
    }
    if (get_reg8(&cpu, REG_C) != 1) {
      fail("set_reg(BC, 1), get_reg(C)=%d, wanted 1", get_reg8(&cpu, REG_C));
    }
    if (get_reg8(&cpu, REG_D) != 0) {
      fail("set_reg(BC, 1), get_reg(D)=%d, wanted 0", get_reg8(&cpu, REG_D));
    }
    if (get_reg8(&cpu, REG_E) != 0) {
      fail("set_reg(BC, 1), get_reg(E)=%d, wanted 0", get_reg8(&cpu, REG_E));
    }
    if (get_reg8(&cpu, REG_H) != 0) {
      fail("set_reg(BC, 1), get_reg(H)=%d, wanted 0", get_reg8(&cpu, REG_H));
    }
    if (get_reg8(&cpu, REG_L) != 0) {
      fail("set_reg(BC, 1), get_reg(L)=%d, wanted 0", get_reg8(&cpu, REG_L));
    }
    if (get_reg8(&cpu, REG_A) != 0) {
      fail("set_reg(BC, 1), get_reg(A)=%d, wanted 0", get_reg8(&cpu, REG_A));
    }
    if (cpu.sp != 0) {
      fail("set_reg(BC, 1), get_reg(SP)=%d, wanted 0", cpu.sp);
    }
  }
  {
    Cpu cpu = {};
    set_reg16_low_high(&cpu, REG_DE, 1, 2);
    if (get_reg16(&cpu, REG_DE) != 0x0201) {
      fail("set_reg(DE, 1), get_reg(DE)=0x%04x, wanted 0x0201",
           get_reg16(&cpu, REG_DE));
    }
    if (get_reg8(&cpu, REG_B) != 0) {
      fail("set_reg(DE, 1), get_reg(B)=%d, wanted 0", get_reg8(&cpu, REG_B));
    }
    if (get_reg8(&cpu, REG_C) != 0) {
      fail("set_reg(DE, 1), get_reg(C)=%d, wanted 0", get_reg8(&cpu, REG_C));
    }
    if (get_reg8(&cpu, REG_D) != 2) {
      fail("set_reg(DE, 1), get_reg(D)=%d, wanted 2", get_reg8(&cpu, REG_D));
    }
    if (get_reg8(&cpu, REG_E) != 1) {
      fail("set_reg(DE, 1), get_reg(E)=%d, wanted 1", get_reg8(&cpu, REG_E));
    }
    if (get_reg8(&cpu, REG_H) != 0) {
      fail("set_reg(DE, 1), get_reg(H)=%d, wanted 0", get_reg8(&cpu, REG_H));
    }
    if (get_reg8(&cpu, REG_L) != 0) {
      fail("set_reg(DE, 1), get_reg(L)=%d, wanted 0", get_reg8(&cpu, REG_L));
    }
    if (get_reg8(&cpu, REG_A) != 0) {
      fail("set_reg(DE, 1), get_reg(A)=%d, wanted 0", get_reg8(&cpu, REG_A));
    }
    if (cpu.sp != 0) {
      fail("set_reg(DE, 1), get_reg(SP)=%d, wanted 0", cpu.sp);
    }
  }
  {
    Cpu cpu = {};
    set_reg16_low_high(&cpu, REG_HL, 1, 2);
    if (get_reg16(&cpu, REG_HL) != 0x0201) {
      fail("set_reg(HL, 1), get_reg(HL)=0x%04x, wanted 0x0201",
           get_reg16(&cpu, REG_HL));
    }
    if (get_reg8(&cpu, REG_B) != 0) {
      fail("set_reg(HL, 1), get_reg(B)=%d, wanted 0", get_reg8(&cpu, REG_B));
    }
    if (get_reg8(&cpu, REG_C) != 0) {
      fail("set_reg(HL, 1), get_reg(C)=%d, wanted 0", get_reg8(&cpu, REG_C));
    }
    if (get_reg8(&cpu, REG_D) != 0) {
      fail("set_reg(HL, 1), get_reg(D)=%d, wanted 0", get_reg8(&cpu, REG_D));
    }
    if (get_reg8(&cpu, REG_E) != 0) {
      fail("set_reg(HL, 1), get_reg(E)=%d, wanted 0", get_reg8(&cpu, REG_E));
    }
    if (get_reg8(&cpu, REG_H) != 2) {
      fail("set_reg(HL, 1), get_reg(H)=%d, wanted 2", get_reg8(&cpu, REG_H));
    }
    if (get_reg8(&cpu, REG_L) != 1) {
      fail("set_reg(HL, 1), get_reg(L)=%d, wanted 1", get_reg8(&cpu, REG_L));
    }
    if (get_reg8(&cpu, REG_A) != 0) {
      fail("set_reg(HL, 1), get_reg(A)=%d, wanted 0", get_reg8(&cpu, REG_A));
    }
    if (cpu.sp != 0) {
      fail("set_reg(HL, 1), get_reg(SP)=%d, wanted 0", cpu.sp);
    }
  }
  {
    Cpu cpu = {};
    set_reg16_low_high(&cpu, REG_SP, 1, 2);
    if (get_reg16(&cpu, REG_SP) != 0x0201) {
      fail("set_reg(SP, 1), get_reg(SP)=0x%04x, wanted 0x0201",
           get_reg16(&cpu, REG_SP));
    }
    if (get_reg8(&cpu, REG_B) != 0) {
      fail("set_reg(SP, 1), get_reg(B)=%d, wanted 0", get_reg8(&cpu, REG_B));
    }
    if (get_reg8(&cpu, REG_C) != 0) {
      fail("set_reg(SP, 1), get_reg(C)=%d, wanted 0", get_reg8(&cpu, REG_C));
    }
    if (get_reg8(&cpu, REG_D) != 0) {
      fail("set_reg(SP, 1), get_reg(D)=%d, wanted 0", get_reg8(&cpu, REG_D));
    }
    if (get_reg8(&cpu, REG_E) != 0) {
      fail("set_reg(SP, 1), get_reg(E)=%d, wanted 0", get_reg8(&cpu, REG_E));
    }
    if (get_reg8(&cpu, REG_H) != 0) {
      fail("set_reg(SP, 1), get_reg(H)=%d, wanted 0", get_reg8(&cpu, REG_H));
    }
    if (get_reg8(&cpu, REG_L) != 0) {
      fail("set_reg(SP, 1), get_reg(L)=%d, wanted 0", get_reg8(&cpu, REG_L));
    }
    if (get_reg8(&cpu, REG_A) != 0) {
      fail("set_reg(SP, 1), get_reg(A)=%d, wanted 0", get_reg8(&cpu, REG_A));
    }
    if (cpu.sp != 0x0201) {
      fail("set_reg(SP, 1), get_reg(SP)=%d, wanted 0x0201", cpu.sp);
    }
  }

  // Test that set_reg16 is using the right byte order.
  {
    Cpu cpu = {};
    set_reg16(&cpu, REG_BC, 0x0102);
    if (get_reg16(&cpu, REG_BC) != 0x0102) {
      fail("set_reg(BC, 1), get_reg(BC)=0x%04x, wanted 0x0201",
           get_reg16(&cpu, REG_BC));
    }
  }
}

struct exec_test {
  const char *name;
  const Gameboy init;
  const Gameboy want;
  int cycles;
};

// We use High RAM below for writes, since we know it's going to be writable,
// whereas the ROM addresses won't be writable.
enum {
  HIGH_RAM_START = 0xFF80,
  FLAGS_NHC = FLAG_N | FLAG_H | FLAG_C,
  FLAGS_ZNH = FLAG_Z | FLAG_N | FLAG_H,
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
};

void run_exec_tests() {
  for (int i = 0; i < sizeof(exec_tests) / sizeof(exec_tests[0]); i++) {
    struct exec_test *test = &exec_tests[i];
    Gameboy g = test->init;
    int cycles = 0;
    do {
      cycles++;
    } while (cycles < 10 && cpu_mcycle(&g));
    if (cycles != test->cycles) {
      fail("%s: got %d cycles, expected %d", test->name, cycles, test->cycles);
    }
    if (!gameboy_eq(&g, &test->want)) {
      gameboy_print_diff(stderr, &g, &test->want);
      fail("%s: Gameboy state does not match expected", test->name);
    }
  }
}

int main() {
  run_snprint_tests();
  run_cb_snprint_tests();
  run_reg8_get_set_tests();
  run_reg16_get_set_tests();
  run_exec_tests();
  return 0;
}
