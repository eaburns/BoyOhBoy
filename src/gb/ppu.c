#include "gameboy.h"

static void store(Gameboy *g, uint16_t addr, uint8_t x) {
  if (g->dma_ticks_remaining > 0 && addr >= MEM_OAM_START &&
      addr <= MEM_OAM_END) {
    // During DMA OAM is inaccessible.
    return;
  }

  if (addr == MEM_LY) {
    // STAT bit 2 indicates whether LY == LYC.
    if (g->mem[MEM_LY] != g->mem[MEM_LYC] && x == g->mem[MEM_LYC]) {
      if (g->mem[MEM_STAT] & STAT_LYC_IRQ) {
        g->mem[MEM_IF] |= IF_LCD;
      }
      g->mem[MEM_STAT] |= STAT_LC_EQ_LYC;
    } else {
      g->mem[MEM_STAT] &= ~(STAT_LC_EQ_LYC);
    }
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

static void set_ppu_mode(Gameboy *g, PpuMode mode) {
  if (ppu_enabled(g) && mode == 0 && (g->mem[MEM_STAT] & STAT_MODE_0_IRQ) ||
      mode == 1 && (g->mem[MEM_STAT] & STAT_MODE_1_IRQ) ||
      mode == 2 && (g->mem[MEM_STAT] & STAT_MODE_2_IRQ)) {
    g->mem[MEM_IF] |= IF_LCD;
  }
  store(g, MEM_STAT, (fetch(g, MEM_STAT) & ~0x3) | mode);
}

bool ppu_enabled(const Gameboy *g) { return g->mem[MEM_LCDC] & LCDC_ENABLED; }

PpuMode ppu_mode(const Gameboy *g) { return g->mem[MEM_STAT] & STAT_PPU_STATE; }

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
  ppu->ticks = 0;
  set_ppu_mode(g, DRAWING);
}

static int tile_from_map(const Gameboy *g, uint16_t map_base, int x, int y) {
  int map_x = x / TILE_WIDTH;
  int map_y = y / TILE_HEIGHT;
  return fetch(g, map_base + (map_y * TILE_MAP_WIDTH) + map_x);
}

static int tile_color_index(const Gameboy *g, uint16_t tile_addr, int x,
                            int y) {
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
  int color_index = tile_color_index(g, addr, x, y);
  uint8_t pallet = fetch(g, MEM_BGP);
  return (pallet >> 2 * color_index) & 0x3;
}

static int get_obj_color_index(const Gameboy *g, const Object *o, int x,
                               int y) {
  int obj_px_x = x - (o->x - TILE_WIDTH);
  if (obj_px_x < 0 || obj_px_x >= TILE_WIDTH) {
    fail("obj_px_x=%d\n", obj_px_x);
  }
  if (o->flags & OBJ_FLAG_X_FLIP) {
    obj_px_x = TILE_WIDTH - obj_px_x - 1;
  }

  int h = obj_height(g);
  int obj_px_y = y - (o->y - TILE_BIG_HEIGHT);
  if (obj_px_y < 0 || obj_px_y >= h) {
    fail("obj_px_y=%d\n", obj_px_y, h);
  }
  if (o->flags & OBJ_FLAG_Y_FLIP) {
    obj_px_y = h - obj_px_y - 1;
  }

  int tile = o->tile;
  if (obj_px_y >= TILE_HEIGHT) {
    tile++;
  }
  return tile_color_index(g, MEM_TILE_BLOCK0_START + tile * 16, obj_px_x,
                          obj_px_y);
}

static int get_obj_px(const Gameboy *g, int x, int y) {
  int color_index = 0;
  const Object *obj = NULL;
  for (int i = 0; i < g->ppu.nobjs; i++) {
    const Object *o = &g->ppu.objs[i];
    if (o->x - 8 > x || o->x - 8 + TILE_WIDTH <= x) {
      continue;
    }
    int ci = get_obj_color_index(g, o, x, y);
    if (ci > 0 && (obj == NULL || obj->x > o->x)) {
      color_index = ci;
      obj = o;
    }
  }
  if (obj == NULL || color_index == 0) {
    return -1; // transparent
  }
  uint16_t pallet_addr = obj->flags & OBJ_FLAG_PALLET ? MEM_OBP1 : MEM_OBP0;
  uint8_t pallet = fetch(g, pallet_addr);
  return (pallet >> 2 * color_index) & 0x3;
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
  int bgy = (y + fetch(g, MEM_SCY)) % (TILE_MAP_HEIGHT*TILE_HEIGHT);
  for (int x = 0; x < SCREEN_WIDTH; x++) {
    int bgx = (x + fetch(g, MEM_SCX)) % (TILE_MAP_WIDTH*TILE_WIDTH);
    int obj_px = get_obj_px(g, x, y);
    if (obj_px >= 0) {
      g->lcd[y][x] = obj_px;
    } else {
      g->lcd[y][x] = tile_map_px(g, bg_tile_map_base, bgx, bgy);
    }
  }
  ppu->ticks = 0;
  set_ppu_mode(g, HBLANK);
}

static void do_hblank(Gameboy *g) {
  Ppu *ppu = &g->ppu;
  if (ppu->ticks < 203) {
    return;
  }
  ppu->ticks = 0;
  int y = fetch(g, MEM_LY);
  set_ppu_mode(g, y < 143 ? OAM_SCAN : VBLANK);
  store(g, MEM_LY, (y + 1) % YMAX);
  if (ppu_mode(g) == VBLANK) {
    store(g, MEM_IF, fetch(g, MEM_IF) | IF_VBLANK);
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
  set_ppu_mode(g, OAM_SCAN);
  store(g, MEM_LY, 0);
}

void ppu_enable(Gameboy *g) {
  set_ppu_mode(g, OAM_SCAN);
  g->ppu.ticks = 0;
  store(g, MEM_LY, 0);
}

void ppu_tcycle(Gameboy *g) {
  Ppu *ppu = &g->ppu;
  if (!ppu_enabled(g)) {
    set_ppu_mode(g, 0);
    ppu->ticks = 0;
    store(g, MEM_LY, 0);
    return;
  }
  ppu->ticks++;
  switch (ppu_mode(g)) {
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
