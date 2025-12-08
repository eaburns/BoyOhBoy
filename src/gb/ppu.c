#include "gameboy.h"

static void store(Gameboy *g, uint16_t addr, uint8_t x) {
  if (g->dma_ticks_remaining > 0 && addr >= MEM_OAM_START &&
      addr <= MEM_OAM_END) {
    // During DMA OAM is inaccessible.
    return;
  }

  g->mem[addr] = x;

  if (addr == MEM_LY) {
    // STAT bit 2 indicates whether LY == LYC.
    if (g->mem[MEM_LY] == g->mem[MEM_LYC]) {
      if (g->mem[MEM_STAT] & MEM_STAT_LYC_IRQ) {
        g->mem[MEM_IF] |= MEM_IF_LCD;
      }
      g->mem[MEM_STAT] |= MEM_STAT_LC_EQ_LYC;
    } else {
      g->mem[MEM_STAT] &= ~(MEM_STAT_LC_EQ_LYC);
    }
  }
}
static uint8_t fetch(const Gameboy *g, uint16_t addr) {
  if (g->dma_ticks_remaining > 0 && addr >= MEM_OAM_START &&
      addr <= MEM_OAM_END) {
    // During DMA OAM is inaccessible.
    return 0xFF;
  }
  return g->mem[addr];
}

static int obj_height(const Gameboy *g) {
  return fetch(g, MEM_LCDC) & LCDC_OBJ_SIZE ? 16 : 8;
}

static void do_oam_scan(Gameboy *g) {
  Ppu *ppu = &g->ppu;
  // OAM scan is 80 ticks total (0-79).
  // Just do all the work on the last tick.
  if (ppu->ticks != 79) {
    return;
  }
  int h = obj_height(g);
  int LY = fetch(g, MEM_LY);
  uint16_t addr = MEM_OAM_START;
  ppu->nobjs = 0;
  while (addr <= MEM_OAM_END) {
    Object o;
    o.y = fetch(g, addr++);
    o.x = fetch(g, addr++);
    o.tile = fetch(g, addr++);
    o.flags = fetch(g, addr++);
    // The PPU only checks the Y coordinate of the object.
    if (o.y - 16 <= LY && o.y - 16 + h > LY && ppu->nobjs < MAX_SCANLINE_OBJS) {
      ppu->objs[ppu->nobjs++] = o;
    }
  }
  ppu->mode = DRAWING;
}

static int tile_from_map(const Gameboy *g, uint16_t map_base, int x, int y) {
  int map_x = x / TILE_WIDTH;
  int map_y = y / TILE_HEIGHT;
  return fetch(g, map_base + (map_y * TILE_MAP_WIDTH) + map_x);
}

static uint8_t tile_px(const Gameboy *g, uint16_t tile_addr, int x, int y) {
  int tile_x = x % TILE_WIDTH;
  int tile_y = y % TILE_HEIGHT;
  uint8_t low = fetch(g, tile_addr + tile_y * 2);
  uint8_t high = fetch(g, tile_addr + tile_y * 2 + 1);
  uint8_t px_low = low >> (7 - tile_x) & 1;
  uint8_t px_high = high >> (7 - tile_x) & 1;
  return px_high << 1 | px_low;
}

static uint8_t tile_map_px(const Gameboy *g, uint16_t map_base, int x, int y) {
  int i = tile_from_map(g, map_base, x, y);
  uint16_t addr = 0;
  if ((fetch(g, MEM_LCDC) >> 4) & 1) {
    addr = MEM_TILE_BLOCK0_START + i * 16;
  } else {
    i = (int8_t)(uint8_t)i; // sign-extend the lower 8 bits of i.
    addr = MEM_TILE_BLOCK2_START + i * 16;
  }
  return tile_px(g, addr, x, y);
}

static uint8_t get_obj_px(const Gameboy *g, int x, int y) {
  const Object *obj = NULL;
  for (int i = 0; i < g->ppu.nobjs; i++) {
    const Object *o = &g->ppu.objs[i];
    if (o->x - 8 > x || o->x - 8 + TILE_WIDTH <= x) {
      continue;
    }
    if (obj == NULL || obj->x > o->x) {
      obj = o;
    }
  }
  if (obj == NULL) {
    return 0;
  }
  int h = obj_height(g);
  int obj_px_x = x - (obj->x - 8);
  if (obj_px_x < 0 || obj_px_x >= 8) {
    fail("obj_px_x=%d\n", obj_px_x);
  }
  int obj_px_y = y - (obj->y - 16);
  if (obj_px_y < 0 || obj_px_y >= h) {
    fail("obj_px_y=%d\n", obj_px_y, h);
  }
  int tile = obj->tile;
  if (obj_px_y >= TILE_HEIGHT) {
    tile++;
  }
  return tile_px(g, MEM_TILE_BLOCK0_START + tile * 16, obj_px_x, obj_px_y);
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
    uint8_t obj_px = get_obj_px(g, x, y);
    if (obj_px > 0) {
      g->lcd[y][x] = obj_px;
    } else {
      g->lcd[y][x] = tile_map_px(g, bg_tile_map_base, bgx, bgy);
    }
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
    store(g, MEM_IF, fetch(g, MEM_IF) | MEM_IF_VBLANK);
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

static void stat_set_ppu_mode(Gameboy *g, PpuMode mode) {
  store(g, MEM_STAT, (fetch(g, MEM_STAT) & ~0x3) | mode);
}

void ppu_tcycle(Gameboy *g) {
  Ppu *ppu = &g->ppu;
  if ((g->mem[MEM_LCDC] & LCDC_ENABLED) == 0) {
    stat_set_ppu_mode(g, 0);
    // Get ready for when the PPU enables.
    // It will start in OAM_SCAN mode.
    ppu->mode = OAM_SCAN;
    ppu->ticks = 0;
    store(g, MEM_LY, 0);
    return;
  }
  ppu->ticks++;
  switch (ppu->mode) {
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
  stat_set_ppu_mode(g, ppu->mode);
}

const char *ppu_mode_name(PpuMode mode) {
  switch (mode) {
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
