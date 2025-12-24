#ifndef GAMEBOY_H
#define GAMEBOY_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// Aborts with a message printf-style message.
void fail(const char *fmt, ...);

enum : uint16_t {
  MEM_ROM_START = 0x0000,
  MEM_ROM_END = 0x7FFF,

  // Rom bank 0.
  MEM_ROM0_START = 0,
  MEM_ROM0_END = 0x3FFF,
  // The start of the cartridge header.
  MEM_HEADER_START = 0x0100,
  MEM_HEADER_TITLE_START = 0x0134,
  MEM_HEADER_TITLE_END = 0x0143,
  MEM_HEADER_GBC_FLAG = 0x0143,
  MEM_HEADER_CART_TYPE = 0x0147,
  MEM_HEADER_ROM_SIZE = 0x0148,
  MEM_HEADER_RAM_SIZE = 0x0149,

  // Rom bank N (depending which is mapped in by the Memory Bank Controller.)
  MEM_ROM_N_START = 0x4000,
  MEM_ROM_N_END = MEM_ROM_END,
  ROM_BANK_SIZE = MEM_ROM_N_END - MEM_ROM_N_START + 1,

  // Video RAM.
  MEM_VRAM_START = 0x8000,
  MEM_TILE_BLOCK0_START = 0x8000,
  MEM_TILE_BLOCK0_END = 0x87FF,
  MEM_TILE_BLOCK1_START = 0x8800,
  MEM_TILE_BLOCK1_END = 0x8FFF,
  MEM_TILE_BLOCK2_START = 0x9000,
  MEM_TILE_BLOCK2_END = 0x97FF,
  MEM_TILE_MAP0_START = 0x9800,
  MEM_TILE_MAP0_END = 0x9BFF,
  MEM_TILE_MAP1_START = 0x9C00,
  MEM_TILE_MAP1_END = 0x9FFF,
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

  // Various mem-mapped I/O.
  MEM_IO_START = 0xFF00,
  MEM_P1_JOYPAD = 0xFF00, // joypad
  MEM_SERIAL_DATA = 0xFF01,
  MEM_SERIAL_CONTROL = 0xFF02,
  // 0xFF03??
  MEM_DIV = 0xFF04,
  MEM_TIMA = 0xFF05,
  MEM_TMA = 0xFF06,
  MEM_TAC = 0xFF07,
  // 0xFF08-0xFF0E??
  MEM_IF = 0xFF0F,
  IF_VBLANK = 1 << 0,
  IF_LCD = 1 << 1,
  MEM_AUDIO_START = 0xFF10,
  MEM_AUDIO_END = 0xFF26,
  // 0xFF27-0xFF2F ??
  MEM_WAVE_START = 0xFF30,
  MEM_WAVE_END = 0xFF3F,
  // …

  MEM_LCDC = 0xFF40,
  LCDC_BG_WIN_ENABLED = 1 << 0,
  LCDC_OBJ_ENABLED = 1 << 1,
  LCDC_OBJ_SIZE = 1 << 2,
  LCDC_BG_TILE_MAP = 1 << 3,
  LCDC_WIN_ENABLED = 1 << 5,
  LCDC_ENABLED = 1 << 7,

  MEM_STAT = 0xFF41,
  // Bits 0 and 1 are the PPU state.
  STAT_PPU_STATE = 0x3,
  STAT_LC_EQ_LYC = 1 << 2,
  STAT_MODE_0_IRQ = 1 << 3,
  STAT_MODE_1_IRQ = 1 << 4,
  STAT_MODE_2_IRQ = 1 << 5,
  STAT_LYC_IRQ = 1 << 6,

  MEM_SCY = 0xFF42,
  MEM_SCX = 0xFF43,
  MEM_LY = 0xFF44,
  MEM_LYC = 0xFF45,
  MEM_DMA = 0xFF46,
  MEM_BGP = 0xFF47,
  MEM_OBP0 = 0xFF48,
  MEM_OBP1 = 0xFF49,
  MEM_WY = 0xFF4A,
  MEM_WX = 0xFF4B,
  // …
  MEM_IO_END = 0xFF7F,

  // High RAM.
  MEM_HIGH_RAM_START = 0xFF80,
  MEM_HIGH_RAM_END = 0xFFFE,

  // The memory address of the IE (interrupts enabled) flags.
  MEM_IE = 0xFFFF,
};

enum { MEM_SIZE = 0x10000 };

typedef uint8_t Mem[MEM_SIZE];

typedef struct mem_names {
  const char *name;
  uint16_t addr;
} MemName;
// An array of names for memory locations, terminated by .name==NULL.
// Some memory locations may have multiple names.
extern const MemName mem_names[];

typedef enum {
  CART_ROM_ONLY = 0x00,
  CART_MBC1 = 0x01,
  CART_MBC1_RAM = 0x02,
  CART_MBC1_RAM_BATTERY = 0x03,
  CART_MBC2 = 0x05,
  CART_MBC2_BATTERY = 0x06,
  CART_ROM_RAM = 0x08,
  CART_ROM_RAM_BATTERY = 0x09,
  CART_MMM01 = 0x0B,
  CART_MMM01_RAM = 0x0C,
  CART_MMM01_RAM_BATTERY = 0x0D,
  CART_MBC3_TIMER_BATTERY = 0x0F,
  CART_MBC3_TIMER_RAM_BATTERY = 0x10,
  CART_MBC3 = 0x11,
  CART_MBC3_RAM = 0x12,
  CART_MBC3_RAM_BATTERY = 0x13,
  CART_MBC5 = 0x19,
  CART_MBC5_RAM = 0x1A,
  CART_MBC5_RAM_BATTERY = 0x1B,
  CART_MBC5_RUMBLE = 0x1C,
  CART_MBC5_RUMBLE_RAM = 0x1D,
  CART_MBC5_RUMBLE_RAM_BATTERY = 0x1E,
  CART_MBC6 = 0x20,
  CART_MBC7_SENSOR_RUMBLE_RAM_BATTERY = 0x22,
  CART_POCKET_CAMERA = 0xFC,
  CART_BANDAI_TAMA5 = 0xFD,
  CART_HuC3 = 0xFE,
  CART_HuC1_RAM_BATTERY = 0xFF,
} CartType;

const char *cart_type_string(CartType cart_type);

typedef struct {
  const uint8_t *data;
  int size;
  char title[MEM_HEADER_TITLE_END - MEM_HEADER_TITLE_START + 1];
  bool gbc;
  CartType cart_type;
  int rom_size;
  int num_rom_banks;
  int ram_size;
} Rom;

// Reads and returns the Rom at path.
// If there is an error reading the file, fail() is called.
// The memory allocated for the returned Rom
// can be freed with free_rom();
Rom read_rom(const char *path);

// Frees any memory allocated for the Rom.
void free_rom(Rom *rom);

typedef uint16_t Addr;

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

typedef struct {
  // The address, data, and instruction's \0-terminated string.
  char full[64];
  // The instruction's \0 terminated string.
  char instr[32];
  // The size of the instruction data in bytes.
  int size;
} Disasm;

// Returns the human-readable version of the instruction at data[offs].
// The size of data is size, and an instruction will not go beyond data+size.
Disasm disassemble(const uint8_t *data, int size, int offs);

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

  REG_F,
  REG_IR,
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

  REG_PC,
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

enum {
  SCREEN_WIDTH = 160,
  SCREEN_HEIGHT = 144,
  YMAX = 153,

  MAX_SCANLINE_OBJS = 10,

  TILE_WIDTH = 8,
  TILE_HEIGHT = 8,
  TILE_BIG_HEIGHT = 16,
  TILE_MAP_WIDTH = 32,
  TILE_MAP_HEIGHT = 32,

  OBJ_FLAG_PRIO = 1 << 7,
  OBJ_FLAG_Y_FLIP = 1 << 6,
  OBJ_FLAG_X_FLIP = 1 << 5,
  OBJ_FLAG_PALLET = 1 << 4,
};

typedef enum {
  HBLANK = 0,
  VBLANK = 1,
  OAM_SCAN = 2,
  DRAWING = 3,
} PpuMode;

const char *ppu_mode_name(PpuMode mode);

typedef struct {
  uint8_t y;
  uint8_t x;
  uint8_t tile;
  uint8_t flags;
} Object;

typedef struct {
  // Ticks counts the number of tcycles in the current mode.
  // (Mode is the lower 2 bits of the STAT register.)
  int ticks;
  // Objects on the current scanline.
  Object objs[MAX_SCANLINE_OBJS];
  int nobjs;
} Ppu;

enum {
  BUTTON_RIGHT = 1 << 0,
  BUTTON_A = 1 << 0,
  BUTTON_LEFT = 1 << 1,
  BUTTON_B = 1 << 1,
  BUTTON_UP = 1 << 2,
  BUTTON_SELECT = 1 << 2,
  BUTTON_DOWN = 1 << 3,
  BUTTON_START = 1 << 3,
  SELECT_BUTTONS = 1 << 5,
  SELECT_DPAD = 1 << 4,
};

enum {
  DMA_SETUP_MCYCLES = 1,
  DMA_MCYCLES = 160,
};

typedef struct {
  Cpu cpu;
  Ppu ppu;
  Mem mem;
  int dma_ticks_remaining;
  const Rom *rom;
  uint8_t lcd[SCREEN_HEIGHT][SCREEN_WIDTH];

  // Bit mask of BUTTON_{A, B, START, SELECT}.
  // A 1 bit means the button is pressed.
  uint8_t buttons;

  // Bit mask of BUTTON_{UP, DOWN, LEFT, RIGHT}.
  // A 1 bit means the button is pressed.
  uint8_t dpad;

  // The system counter is incremented ever T-cycle.
  // The DIV register is the upper 8 bits of the counter.
  uint16_t counter;

  // For debugging; can set this to true to cause the debugger to break.
  bool break_point;
} Gameboy;

// Returns a new Gameboy for the given Rom.
// The Gameboy maintains a pointer to rom,
// so rom must outlive the use of the returned Gameboy.
Gameboy init_gameboy(const Rom *rom);

void ppu_enable(Gameboy *g);
bool ppu_enabled(const Gameboy *g);
PpuMode ppu_mode(const Gameboy *g);

// Executes a single "M cycle" of the entire Gameboy.
// The Gameboy clock ticks at 2²² Hz.
// Each clock tick is referred to as a T cycle.
// The PPU, for example, makes progress every T cycle.
// However, the CPU make logical progress only every 4 T cycles.
// This is referred to as an M cycle — 4 T cycles == 1 M cycle.
// This function executes a single M cycle of the CPU
// followed by 4 T cycles of the PPU,
// and any relevant cycles of other systems such as OAM DMA.
void mcycle(Gameboy *g);

// Executes a single T cycle of the PPU.
void ppu_tcycle(Gameboy *g);

// Executes a single M cycle of the CPU.
void cpu_mcycle(Gameboy *g);

// Returns a string describing the difference between a and b or NULL if they
// are the same.
char *gameboy_diff(const Gameboy *a, const Gameboy *b);

#endif // GAMEBOY_H
