#pragma once

#include "common.h"
#include "defines.h"
#include "gfx.h"
#include "sound.h"
#include "lua_bridge.h"

#include <array>
#include <random>
#include <cstring>

namespace retro8
{
  namespace address
  {
    static constexpr address_t SPRITE_SHEET = 0x0000;
    static constexpr address_t SPRITE_FLAGS = 0x3000;

    static constexpr address_t MUSIC = 0x3100;
    static constexpr address_t SOUNDS = 0x3200;

    static constexpr address_t CART_DATA = 0x5e00;
    static constexpr address_t PALETTES = 0x5f00;
    static constexpr address_t CLIP_RECT = 0x5f20;
    static constexpr address_t PEN_COLOR = 0x5f25;
    static constexpr address_t CURSOR = 0x5f26;
    static constexpr address_t CAMERA = 0x5f28;
    static constexpr address_t FILL_PATTERN = 0x5f31;
    static constexpr address_t DRAW_FLAGS = 0x5f36;
    static constexpr address_t GFX_MAPPING = 0x5f54;
    static constexpr address_t SCREEN_MAPPING = 0x5f55;
    static constexpr address_t MAP_MAPPING = 0x5f56;
    static constexpr address_t MAP_WIDTH = 0x5f57;
    static constexpr address_t SGET_DEFAULT = 0x5f59;
    static constexpr address_t MGET_DEFAULT = 0x5f5a;

    static constexpr address_t SCREEN_DATA = 0x6000;

    static constexpr address_t TILE_MAP_LOW = 0x1000;
    static constexpr address_t TILE_MAP_HIGH = 0x2000;

    static constexpr int32_t CART_DATA_LENGTH = 0x4300;
  };

  class Memory
  {
  public:
    static constexpr int32_t MEMORY_SIZE = 0x10000;

  private:
    uint8_t _backup[address::CART_DATA_LENGTH];
    uint8_t memory[MEMORY_SIZE];

    static constexpr size_t BYTES_PER_PALETTE = sizeof(retro8::gfx::palette_t);
    static constexpr size_t BYTES_PER_SPRITE = sizeof(retro8::gfx::sprite_t);

    static constexpr size_t ROWS_PER_TILE_MAP_HALF = 32;


  public:
    Memory()
    {
      memset(memory, 0, MEMORY_SIZE);
      memset(_backup, 0, sizeof(_backup));
      paletteAt(gfx::DRAW_PALETTE_INDEX)->reset();
      paletteAt(gfx::SCREEN_PALETTE_INDEX)->resetScreen();
      clipRect()->reset();
      cursor()->reset();
      resetMapping();
      memory[0x5f5e] = 0xff;
    }

    /* 0x5f54 picks the sprite sheet, 0x5f55 the screen that drawing goes to, and 0x5f56 and 0x5f57 the map and its width */
    void resetMapping()
    {
      memory[address::GFX_MAPPING] = 0x00;
      memory[address::SCREEN_MAPPING] = 0x60;
      memory[address::MAP_MAPPING] = 0x20;
      memory[address::MAP_WIDTH] = 0x80;
    }

    address_t mapped(address_t reg) const
    {
      uint8_t page = memory[reg];
      if (reg == address::MAP_MAPPING && page >= 0x30 && page <= 0x3f)
        page -= 0x20;
      const address_t base = address_t(page) << 8;
      return base > 0xe000 ? 0xe000 : base;
    }

    address_t dataAddress(address_t addr) const
    {
      addr &= 0xffff;
      if (addr < 0x2000)
        return mapped(address::GFX_MAPPING) + addr;
      if (addr >= 0x6000 && addr < 0x8000)
        return mapped(address::SCREEN_MAPPING) + addr - 0x6000;
      return addr;
    }

    bool identityMapping() const
    {
      return memory[address::GFX_MAPPING] == 0 && memory[address::SCREEN_MAPPING] == 0x60;
    }

    int32_t mapWidth() const { return memory[address::MAP_WIDTH] ? memory[address::MAP_WIDTH] : 256; }
    bool legacyMap() const { return memory[address::MAP_MAPPING] == 0x20 && mapWidth() == 128; }
    int32_t mapHeight() const
    {
      if (legacyMap())
        return 64;
      const address_t base = mapped(address::MAP_MAPPING);
      const address_t end = base >= 0x8000 ? 0x10000 : 0x3000;
      return base < end ? int32_t((end - base) / mapWidth()) : 0;
    }
    bool inMap(coord_t x, coord_t y) const { return x >= 0 && y >= 0 && x < mapWidth() && y < mapHeight(); }

    void backupCartridge()
    {
      std::memcpy(_backup, memory, address::CART_DATA_LENGTH);
    }

    const uint8_t* backup() const { return _backup; }
    uint8_t* backupData() { return _backup; }
    uint8_t* base() { return memory; }

    gfx::color_byte_t* penColor() { return as<gfx::color_byte_t>(address::PEN_COLOR); }
    gfx::cursor_t* cursor() { return as<gfx::cursor_t>(address::CURSOR); }
    gfx::camera_t* camera() { return as<gfx::camera_t>(address::CAMERA); }
    gfx::clip_rect_t* clipRect() { return as<gfx::clip_rect_t>(address::CLIP_RECT); }

    gfx::color_byte_t* spriteSheet(coord_t x, coord_t y) { return spriteSheet() + x / gfx::PIXEL_TO_BYTE_RATIO + y * gfx::SPRITE_SHEET_PITCH; }
    gfx::color_byte_t* spriteSheet() { return as<gfx::color_byte_t>(mapped(address::GFX_MAPPING)); }
    gfx::color_byte_t* screenData() { return as<gfx::color_byte_t>(mapped(address::SCREEN_MAPPING)); }
    gfx::color_byte_t* displayData() { return as<gfx::color_byte_t>(address::SCREEN_DATA); }
    gfx::color_byte_t* screenData(coord_t x, coord_t y) { return screenData() + y * gfx::SCREEN_PITCH + x / gfx::PIXEL_TO_BYTE_RATIO; }
    integral_t* cartData(index_t idx) { return as<integral_t>(address::CART_DATA + idx * sizeof(integral_t)); } //TODO: ENDIANNESS!!

#if SOUND_ENABLED
    sfx::Sound* sound(sfx::sound_index_t i) { return as<sfx::Sound>(address::SOUNDS + sizeof(sfx::Sound)*i); }
    sfx::Music* music(sfx::music_index_t i) { return as<sfx::Music>(address::MUSIC + sizeof(sfx::Music)*i); }
#endif

    sprite_flags_t* spriteFlagsFor(sprite_index_t index)
    {
      return as<sprite_flags_t>(address::SPRITE_FLAGS + index);
    }

    sprite_index_t* spriteInTileMap(coord_t x, coord_t y)
    {
      static_assert(sizeof(sprite_index_t) == 1, "sprite_index_t must be 1 byte");

      sprite_index_t *addr;

      if (!legacyMap())
        addr = as<sprite_index_t>(mapped(address::MAP_MAPPING) + y * mapWidth() + x);
      else if (y >= static_cast<coord_t>(ROWS_PER_TILE_MAP_HALF))
        addr = as<sprite_index_t>(address::TILE_MAP_LOW) + x + (y - ROWS_PER_TILE_MAP_HALF) * gfx::TILE_MAP_WIDTH * sizeof(sprite_index_t);
      else
        addr = as<sprite_index_t>(address::TILE_MAP_HIGH) + x + y * gfx::TILE_MAP_WIDTH * sizeof(sprite_index_t);

      return addr;
    }

    gfx::sprite_t* spriteAt(sprite_index_t index) {
      return reinterpret_cast<gfx::sprite_t*>(&memory[mapped(address::GFX_MAPPING)
        + (index % gfx::SPRITES_PER_SPRITE_SHEET_ROW) * gfx::SPRITE_BYTES_PER_SPRITE_ROW]
        + (index / gfx::SPRITES_PER_SPRITE_SHEET_ROW) * gfx::SPRITE_SHEET_PITCH * gfx::SPRITE_HEIGHT
        ); }
    /* the screen palette entry colour c shows through on line y. 0x5f5f at 0x10 swaps in the palette at 0x5f60 on the
       lines whose bit is set in 0x5f70 to 0x5f7f, and 0x3n turns colour n into a gradient down that palette, a step every
       8 lines and a step further on lines whose bit is set */
    uint8_t screenEntry(color_t c, coord_t y) const
    {
      const uint8_t mode = memory[0x5f5f];
      const bool marked = (memory[0x5f70 + (y >> 3)] >> (y & 7)) & 1;
      if (mode == 0x10 && marked)
        return memory[0x5f60 + c];
      if ((mode & 0x30) == 0x30 && (mode & 0x0f) == c)
        return memory[0x5f60 + ((y / 8 + marked) & 0x0f)];
      return memory[address::PALETTES + gfx::SCREEN_PALETTE_INDEX * BYTES_PER_PALETTE + c];
    }

    gfx::palette_t* paletteAt(palette_index_t index) { return reinterpret_cast<gfx::palette_t*>(&memory[address::PALETTES + index * BYTES_PER_PALETTE]); }

    template<typename T> T* as(address_t addr) { return reinterpret_cast<T*>(&memory[addr]); }
  };
}
