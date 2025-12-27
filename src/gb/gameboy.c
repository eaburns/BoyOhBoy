#include "gameboy.h"

#include "buf/buffer.h"
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void fail(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");
  abort();
}

const char *cart_type_string(CartType cart_type) {
  switch (cart_type) {
  case CART_ROM_ONLY:
    return "ROM ONLY";
  case CART_MBC1:
    return "MBC1";
  case CART_MBC1_RAM:
    return "MBC1 + RAM";
  case CART_MBC1_RAM_BATTERY:
    return "MBC1 + RAM + BATTERY";
  case CART_MBC2:
    return "MBC2";
  case CART_MBC2_BATTERY:
    return "MBC2 + BATTERY";
  case CART_ROM_RAM:
    return "ROM + RAM";
  case CART_ROM_RAM_BATTERY:
    return "ROM + RAM + BATTERY";
  case CART_MMM01:
    return "MMM01";
  case CART_MMM01_RAM:
    return "MMM01 + RAM";
  case CART_MMM01_RAM_BATTERY:
    return "MMM01 + RAM + BATTERY";
  case CART_MBC3_TIMER_BATTERY:
    return "MBC3 + TIMER + BATTERY";
  case CART_MBC3_TIMER_RAM_BATTERY:
    return "MBC3 + TIMER + RAM + BATTERY";
  case CART_MBC3:
    return "MBC3";
  case CART_MBC3_RAM:
    return "MBC3 + RAM";
  case CART_MBC3_RAM_BATTERY:
    return "MBC3 + RAM + BATTERY";
  case CART_MBC5:
    return "MBC5";
  case CART_MBC5_RAM:
    return "MBC5 + RAM";
  case CART_MBC5_RAM_BATTERY:
    return "MBC5 + RAM + BATTERY";
  case CART_MBC5_RUMBLE:
    return "MBC5 + RUMBLE";
  case CART_MBC5_RUMBLE_RAM:
    return "MBC5 + RUMBLE + RAM";
  case CART_MBC5_RUMBLE_RAM_BATTERY:
    return "MBC5 + RUMBLE + RAM + BATTERY";
  case CART_MBC6:
    return "MBC6";
  case CART_MBC7_SENSOR_RUMBLE_RAM_BATTERY:
    return "MBC7 + SENSOR + RUMBLE + RAM + BATTERY";
  case CART_POCKET_CAMERA:
    return "POCKET CAMERA";
  case CART_BANDAI_TAMA5:
    return "BANDAI TAMA5";
  case CART_HuC3:
    return "HuC3";
  case CART_HuC1_RAM_BATTERY:
    return "HuC1 + RAM + BATTERY";
  default:
    return "UNKNOWN";
  }
}

Rom read_rom(const char *path) {
  FILE *in = fopen(path, "r");
  if (in == NULL) {
    fail("failed to open %s: %s", path, strerror(errno));
  }
  int size = 0;
  uint8_t *data = NULL;
  for (;;) {
    char buf[4096];
    int n = fread(buf, 1, sizeof(buf), in);
    data = realloc(data, size + n);
    memcpy(data + size, buf, n);
    size += n;
    if (n < sizeof(buf)) {
      break;
    }
  }
  if (ferror(in)) {
    fail("failed to read from %s: %s", path, strerror(errno));
  }
  if (fclose(in) != 0) {
    fail("failed to close %s: %s", path, strerror(errno));
  }
  Rom rom = {
      .data = data,
      .size = size,
      .gbc = data[MEM_HEADER_GBC_FLAG],
      .cart_type = data[MEM_HEADER_CART_TYPE],
  };
  memcpy(rom.title, data + MEM_HEADER_TITLE_START, sizeof(rom.title) - 1);

  switch (data[MEM_HEADER_ROM_SIZE]) {
  case 0:
    rom.rom_size = 1 << 15;
    rom.num_rom_banks = 2;
    break;
  case 1:
    rom.rom_size = 1 << 16;
    rom.num_rom_banks = 4;
    break;
  case 2:
    rom.rom_size = 1 << 17;
    rom.num_rom_banks = 8;
    break;
  case 3:
    rom.rom_size = 1 << 18;
    rom.num_rom_banks = 16;
    break;
  case 4:
    rom.rom_size = 1 << 19;
    rom.num_rom_banks = 32;
    break;
  case 5:
    rom.rom_size = 1 << 20;
    rom.num_rom_banks = 64;
    break;
  case 6:
    rom.rom_size = 1 << 21;
    rom.num_rom_banks = 128;
    break;
  case 7:
    rom.rom_size = 1 << 22;
    rom.num_rom_banks = 256;
    break;
  case 8:
    rom.rom_size = 1 << 23;
    rom.num_rom_banks = 512;
    break;
  default:
    fprintf(stderr, "Unknown ROM size indicator: %d\n",
            data[MEM_HEADER_ROM_SIZE]);
  }

  switch (data[MEM_HEADER_RAM_SIZE]) {
  case 0:
    rom.ram_size = 0;
    break;
  case 2:
    rom.ram_size = 8192;
    break;
  case 3:
    rom.ram_size = 32768;
    break;
  case 4:
    rom.ram_size = 131072;
    break;
  case 5:
    rom.ram_size = 65536;
    break;
  case 1:
  // unused -- fallthrough intended
  default:
    fprintf(stderr, "Unknown RAM size indicator: %d\n",
            data[MEM_HEADER_RAM_SIZE]);
  }
  return rom;
}

void free_rom(Rom *rom) { free((void *)rom->data); }

Gameboy init_gameboy(const Rom *rom) {
  Gameboy g = {};
  g.rom = rom;

  int rom_size = MEM_ROM_END + 1;
  if (rom->size < rom_size) {
    rom_size = rom->size;
  }
  memcpy(g.mem, rom->data, rom_size);

  // Starting state of DMG after running the boot ROM and ending at 0x0101.
  g.cpu.registers[REG_B] = 0x00;
  g.cpu.registers[REG_C] = 0x13;
  g.cpu.registers[REG_D] = 0x00;
  g.cpu.registers[REG_E] = 0xD3;
  g.cpu.registers[REG_H] = 0x01;
  g.cpu.registers[REG_L] = 0x4D;
  g.cpu.registers[REG_A] = 0x01;
  g.cpu.ir = 0x0; // NOP
  g.cpu.pc = 0x0101;
  g.cpu.sp = 0xFFFE;
  g.cpu.flags = FLAG_Z;
  g.mem[MEM_P1_JOYPAD] = 0xCF;
  g.mem[MEM_DIV] = 0xAB;
  g.mem[MEM_TAC] = 0xF8;
  g.mem[MEM_IF] = 0xE1;
  g.mem[MEM_LCDC] = 0x91;
  g.mem[MEM_STAT] = 0x85;
  g.mem[MEM_DMA] = 0xFF;
  g.mem[MEM_BGP] = 0xFC;
  return g;
}

static void do_oam_dma(Gameboy *g) {
  if (g->dma_ticks_remaining <= 0) {
    return;
  }
  if (g->dma_ticks_remaining > DMA_MCYCLES) {
    g->dma_ticks_remaining--;
    return;
  }
  uint16_t offs = DMA_MCYCLES - g->dma_ticks_remaining;
  uint16_t src = g->mem[MEM_DMA] * 0x100 + offs;
  uint16_t dst = MEM_OAM_START + offs;
  g->mem[dst] = g->mem[src];
  g->dma_ticks_remaining--;
}

// Returns the value of the tima counter bit, which is AND
// of the TIMA enabled bit of TAC and the corresponding
// frequency bit of the system counter.
static bool tima_bit(const Gameboy *g) {
  int tima_shift = 2 * (g->mem[MEM_TAC] & TAC_FREQ_MASK) + 1;
  bool counter_bit = (g->counter >> tima_shift) & 0x1;
  bool tima_enabled = g->mem[MEM_TAC] & TAC_TIMA_ENABLED;
  return counter_bit && tima_enabled;
}

static bool inc_counter(Gameboy *g, bool tima_bit_start) {
  g->counter++;
  g->mem[MEM_DIV] = g->counter >> 8;

  // TIMA increments based on a falling edge,
  // so we need to track the previous value
  // and compare to the current.
  bool tima_bit_end = tima_bit(g);
  if (tima_bit_start && !tima_bit_end) {
    g->mem[MEM_TIMA]++;
    if (g->mem[MEM_TIMA] == 0) {
      g->mem[MEM_TIMA] = g->mem[MEM_TMA];
      g->mem[MEM_IF] |= IF_TIMER;
    }
  }
  return tima_bit_end;
}

void mcycle(Gameboy *g) {
  do {
    // We increment the counter once before calling cpu_mcycle,
    // so that if the CPU writes to DIV, resetting the counter,
    // the reset resets this single count.
    // Additionally, TIMA is incremented based on a falling edge detector
    // so we need to track the previous bit value of tima_bit()
    // and pass it to inc_counter so it can detect a falling edge.
    bool tb = inc_counter(g, tima_bit(g));

    cpu_mcycle(g);
    do_oam_dma(g);
    ppu_tcycle(g);

    for (int i = 0; i < 3; i++) {
      ppu_tcycle(g);
      tb = inc_counter(g, tb);
    }
  } while (g->cpu.state == EXECUTING || g->cpu.state == INTERRUPTING);
}

char *gameboy_diff(const Gameboy *a, const Gameboy *b) {
  Buffer buf = {};
  for (int i = 0; i < sizeof(a->cpu.registers); i++) {
    if (a->cpu.registers[i] != b->cpu.registers[i]) {
      bprintf(&buf, "registers[%s]: %d ($%02X) != %d ($%02X) \n", reg8_name(i),
              a->cpu.registers[i], a->cpu.registers[i], b->cpu.registers[i],
              b->cpu.registers[i]);
    }
  }
  if (a->cpu.flags != b->cpu.flags) {
    bprintf(&buf, "flags: $%02X != $%02X\n", a->cpu.flags, b->cpu.flags);
  }
  if (a->cpu.sp != b->cpu.sp) {
    bprintf(&buf, "sp: %d ($%02X) != %d ($%02X)\n", a->cpu.sp, a->cpu.sp,
            b->cpu.sp, b->cpu.sp);
  }
  if (a->cpu.pc != b->cpu.pc) {
    bprintf(&buf, "pc: %d ($%02X) != %d ($%02X)\n", a->cpu.pc, a->cpu.pc,
            b->cpu.pc, b->cpu.pc);
  }
  if (a->cpu.ir != b->cpu.ir) {
    bprintf(&buf, "ir: %d ($%02X) != %d ($%02X)\n", a->cpu.ir, a->cpu.ir,
            b->cpu.ir, b->cpu.ir);
  }
  if (a->cpu.ime != b->cpu.ime) {
    bprintf(&buf, "ime: %d != %d\n", a->cpu.ime, b->cpu.ime);
  }
  if (a->cpu.ei_pend != b->cpu.ei_pend) {
    bprintf(&buf, "ei_pend: %d != %d\n", a->cpu.ei_pend, b->cpu.ei_pend);
  }
  if (a->cpu.state != b->cpu.state) {
    bprintf(&buf, "state: %s != %s\n", cpu_state_name(a->cpu.state),
            cpu_state_name(b->cpu.state));
  }
  if (!(a->cpu.bank == b->cpu.bank ||
        a->cpu.bank == NULL && b->cpu.bank == instructions ||
        a->cpu.bank == instructions && b->cpu.bank == NULL)) {
    bprintf(&buf, "bank: %p != %p\n", a->cpu.bank, b->cpu.bank);
  }
  if (a->cpu.cycle != b->cpu.cycle) {
    bprintf(&buf, "cycle: %d != %d\n", a->cpu.cycle, b->cpu.cycle);
  }
  if (a->cpu.w != b->cpu.w) {
    bprintf(&buf, "w: %d ($%02X) != %d ($%02X)\n", a->cpu.w, a->cpu.w, b->cpu.w,
            b->cpu.w);
  }
  if (a->cpu.z != b->cpu.z) {
    bprintf(&buf, "z: %d ($%02X) != %d ($%02X)\n", a->cpu.z, a->cpu.z, b->cpu.z,
            b->cpu.z);
  }
  if (a->ppu.ticks != b->ppu.ticks) {
    bprintf(&buf, "ppu.ticks: %d != %d\n", a->ppu.ticks, b->ppu.ticks);
  }
  if (a->ppu.nobjs != b->ppu.nobjs) {
    bprintf(&buf, "ppu.nobjs: %d != %d\n", a->ppu.nobjs, b->ppu.nobjs);
  } else {
    for (int i = 0; i < a->ppu.nobjs; i++) {
      const Object *ao = &a->ppu.objs[i];
      const Object *bo = &b->ppu.objs[i];
      if (ao->x != bo->x) {
        bprintf(&buf, "ppu.objs[%d].x: %d != %d\n", ao->x, bo->x);
      }
      if (ao->y != bo->y) {
        bprintf(&buf, "ppu.objs[%d].y: %d != %d\n", ao->y, bo->y);
      }
      if (ao->tile != bo->tile) {
        bprintf(&buf, "ppu.objs[%d].tile: %d != %d\n", ao->tile, bo->tile);
      }
      if (ao->flags != bo->flags) {
        bprintf(&buf, "ppu.objs[%d].flags: $%02X != $%02X\n", ao->flags,
                bo->flags);
      }
    }
  }
  if (a->dma_ticks_remaining != b->dma_ticks_remaining) {
    bprintf(&buf, "dma_ticks_remaining: %d != %d\n", a->dma_ticks_remaining,
            b->dma_ticks_remaining);
  }
  if (a->buttons != b->buttons) {
    bprintf(&buf, "buttons: %02X != %02X\n", a->buttons, b->buttons);
  }
  if (a->dpad != b->dpad) {
    bprintf(&buf, "dpad: %02X != %02X\n", a->dpad, b->dpad);
  }
  if (a->counter != b->counter) {
    bprintf(&buf, "counter: %d != %d\n", a->counter, b->counter);
  }
  for (int i = 0; i < sizeof(a->mem); i++) {
    if (a->mem[i] != b->mem[i]) {
      bprintf(&buf, "mem[$%04X]: %d ($%02X) != %d ($%02X)\n", i, a->mem[i],
              a->mem[i], b->mem[i], b->mem[i]);
    }
  }

  // Try to print a nicer diff of the LCD.
  int ymin = SCREEN_HEIGHT;
  int ymax = -1;
  int xmin = SCREEN_WIDTH;
  int xmax = -1;
  for (int y = 0; y < SCREEN_HEIGHT; y++) {
    for (int x = 0; x < SCREEN_WIDTH; x++) {
      if (a->lcd[y][x] != b->lcd[y][x]) {
        ymin = y < ymin ? y : ymin;
        ymax = y > ymax ? y : ymax;
        xmin = x < xmin ? x : xmin;
        xmax = x > xmax ? x : xmax;
      }
    }
  }
  if (ymax >= 0) {
    bprintf(&buf, "LCD diff\n    ");
    for (int x = xmin; x <= xmax; x++) {
      bprintf(&buf, " %3d", x);
    }
    bprintf(&buf, "\n    +");
    for (int x = xmin; x <= xmax; x++) {
      if (x > xmin) {
        bprintf(&buf, "-");
      }
      bprintf(&buf, "----", x);
    }
    bprintf(&buf, "\n");
    for (int y = ymin; y <= ymax; y++) {
      bprintf(&buf, "%3d | ", y);
      for (int x = xmin; x <= xmax; x++) {
        if (x > xmin) {
          bprintf(&buf, " ");
        }
        if (a->lcd[y][x] != b->lcd[y][x]) {
          bprintf(&buf, "%d≠%d", a->lcd[y][x], b->lcd[y][x]);
        } else {
          bprintf(&buf, " %d ", a->lcd[y][x]);
        }
      }
      bprintf(&buf, "\n");
    }
  }
  return buf.data;
}
