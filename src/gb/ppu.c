#include "gameboy.h"

static void store(Gameboy *g, uint16_t addr, uint8_t x) {
  if (g->dma_ticks_remaining > 0 && addr >= MEM_OAM_START &&
      addr <= MEM_OAM_END) {
    // During DMA OAM is inaccessible.
    return;
  }
  g->mem[addr] = x;
}
static uint8_t fetch(const Gameboy *g, uint16_t addr) {
  if (g->dma_ticks_remaining > 0 && addr >= MEM_OAM_START &&
      addr <= MEM_OAM_END) {
    // During DMA OAM is inaccessible.
    return 0xFF;
  }
  return g->mem[addr];
}

void ppu_tcycle(Gameboy *g) {
  Ppu *ppu = &g->ppu;
  if ((g->mem[MEM_LCDC] & 0x80) == 0) {
    ppu->mode = STOPPED;
    return;
  }
  ppu->ticks++;
  switch (ppu->mode) {
  case STOPPED:
    ppu->mode = OAM_SCAN;
    ppu->ticks = 0;
    // FALLTHROUGH
  case OAM_SCAN:
    if (ppu->ticks == 80) {
      ppu->ticks = 0;
      ppu->mode = DRAWING;
    }
    break;
  case DRAWING:
    if (ppu->ticks == 172) {
      ppu->ticks = 0;
      ppu->mode = HORIZONTAL_BLANK;
    }
    if (ppu->x < XMAX) {
      ppu->x++;
    }
    break;
  case HORIZONTAL_BLANK:
    if (ppu->ticks == 456) {
      ppu->ticks = 0;
      ppu->mode = OAM_SCAN;
      int y = fetch(g, MEM_LY);
      store(g, MEM_LY, (y + 1) % YMAX);
    }
    break;
  default:
    fail("impossible PPU mode %d\n", ppu->mode);
  }
}

const char *ppu_mode_name(PpuMode mode) {
  switch (mode) {
  case OAM_SCAN:
    return "OAM SCAN";
  case DRAWING:
    return "DRAWING";
  case HORIZONTAL_BLANK:
    return "HORIZONTAL BLANK";
  default:
    return "UNKNOWN";
  }
}
