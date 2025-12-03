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
  ppu->nobjs = 0;
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

static uint8_t px_from_tile_map(const Gameboy *g, uint16_t tile_map_base, int x,
                                int y) {
  int tile_map_y = y / TILE_HEIGHT;
  int tile_map_x = x / TILE_WIDTH;
  int tile_index =
      fetch(g, tile_map_base + (tile_map_y * TILE_MAP_WIDTH) + tile_map_x);
  // TODO: support non-map0-based indexing.
  uint16_t tile_base = MEM_TILE_BLOCK0_START + tile_index * 16;
  int tile_x = x % TILE_WIDTH;
  int tile_y = y % TILE_HEIGHT;
  uint8_t tile_low = fetch(g, tile_base + tile_y * 2);
  uint8_t tile_high = fetch(g, tile_base + tile_y * 2 + 1);
  uint8_t px_low = tile_low >> (7 - tile_x) & 1;
  uint8_t px_high = tile_high >> (7 - tile_x) & 1;
  return px_high << 1 | px_low;
}

static void do_drawing(Gameboy *g) {
  Ppu *ppu = &g->ppu;
  if (ppu->ticks < 171) {
    return;
  }
  // For the time being, just burn 172 cycles and then just draw a scanline.
  uint16_t bg_tile_map_base = MEM_TILE_MAP0_START;
  if (fetch(g, MEM_LCDC) & LCDC_BG_TILE_MAP) {
    bg_tile_map_base = MEM_TILE_MAP1_START;
  }
  int y = fetch(g, MEM_LY);
  int bgy = y + fetch(g, MEM_SCY);
  for (int x = 0; x < SCREEN_WIDTH; x++) {
    int bgx = x + fetch(g, MEM_SCX);
    g->lcd[y][x] = px_from_tile_map(g, bg_tile_map_base, bgx, bgy);
  }
  ppu->ticks = 0;
  ppu->mode = HBLANK;
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
  if (ppu->mode == VBLANK) {
    store(g, MEM_IF, fetch(g, MEM_IF) | 1 << 0);
  }
}

static void do_vblank(Gameboy *g) {
  Ppu *ppu = &g->ppu;
  if (ppu->ticks < 455) {
    return;
  }
  ppu->ticks = 0;
  int y = fetch(g, MEM_LY);
  if (y < YMAX) {
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
