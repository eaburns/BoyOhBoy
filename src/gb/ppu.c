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

static void do_oam_scan(Gameboy *g) {
  Ppu *ppu = &g->ppu;
  // OAM scan is 80 ticks total (0-79).
  // Just do all the work on the last tick.
  if (ppu->ticks != 79) {
    return;
  }
  int h = (fetch(g, MEM_LCDC) & LCDC_OBJ_SIZE) ? 16 : 8;
  int LY = fetch(g, MEM_LY);
  uint16_t addr = MEM_OAM_START;
  while (addr <= MEM_OAM_END) {
    Object o;
    o.y = fetch(g, addr++);
    o.x = fetch(g, addr++);
    o.tile = fetch(g, addr++);
    o.flags = fetch(g, addr++);
    if (o.x != 0 && o.y >= LY + 16 && o.y + h < LY + 16 &&
        ppu->nobjs < MAX_SCANLINE_OBJS) {
      ppu->objs[ppu->nobjs++] = o;
    }
  }
  ppu->mode = DRAWING;
}

static void do_drawing(Gameboy *g) {
  Ppu *ppu = &g->ppu;
  if (ppu->ticks == 171) {
    ppu->ticks = 0;
    ppu->mode = HBLANK;
    return;
  }
  if (ppu->x < XMAX) {
    ppu->x++;
  }
}

static void do_hblank(Gameboy *g) {
  Ppu *ppu = &g->ppu;
  if (ppu->ticks < 455) {
    return;
  }
  ppu->ticks = 0;
  int y = fetch(g, MEM_LY);
  ppu->mode = y < 143 ? OAM_SCAN : VBLANK;
  store(g, MEM_LY, (y + 1) % YMAX);
}

static void do_vblank(Gameboy *g) {
  Ppu *ppu = &g->ppu;
  if (ppu->ticks < 455) {
    return;
  }
  ppu->ticks = 0;
  int y = fetch(g, MEM_LY);
  if (y < 153) {
    store(g, MEM_LY, y + 1);
    return;
  }
  ppu->mode = OAM_SCAN;
  store(g, MEM_LY, 0);
}

void ppu_tcycle(Gameboy *g) {
  Ppu *ppu = &g->ppu;
  if ((g->mem[MEM_LCDC] & LCDC_ENABLED) == 0) {
    ppu->mode = STOPPED;
    return;
  }
  ppu->ticks++;
  switch (ppu->mode) {
  case STOPPED:
    // We were stopped, but LCDC_ENABLED was 1,
    // so we are starting up in OAM_SCAN mode.
    ppu->mode = OAM_SCAN;
    ppu->x = 0;
    ppu->ticks = 0;
    store(g, MEM_LY, 0);
    // FALLTHROUGH
  case OAM_SCAN:
    do_oam_scan(g);
    break;
  case DRAWING:
    do_drawing(g);
    break;
  case HBLANK:
    do_hblank(g);
    break;
  case VBLANK:
    do_vblank(g);
    break;
  }
}

const char *ppu_mode_name(PpuMode mode) {
  switch (mode) {
  case STOPPED:
    return "STOPPED";
  case OAM_SCAN:
    return "OAM SCAN";
  case DRAWING:
    return "DRAWING";
  case HBLANK:
    return "HBLANK";
  case VBLANK:
    return "VBLANK";
  }
  return "UNKNOWN";
}
