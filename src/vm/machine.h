#pragma once

#include "common.h"
#include "defines.h"
#include "gfx.h"
#include "sound.h"
#include "lua_bridge.h"
#include "memory.h"

#include <array>
#include <string>
#include <random>

namespace retro8
{
  class State
  {
  public:
    std::mt19937 rnd;
    point_t lastLineEnd;
    std::array<bit_mask<button_t>, PLAYER_COUNT> buttons;
    std::array<bit_mask<button_t>, PLAYER_COUNT> previousButtons;

    /* the devkit mouse the frontend fills while 0x5f2d bit 0 is set: the position, the buttons packed as stat(34) gives them,
       and the wheel and movement gathered since the cart last ran a frame */
    struct Mouse
    {
      int32_t x = 64, y = 64;
      uint8_t buttons = 0;
      int32_t wheel = 0;
      int32_t dx = 0, dy = 0;
    } mouse;

    /* the devkit keyboard: keys held by SDL scancode for stat(28), and characters typed since stat(31) last took one */
    struct Keyboard
    {
      std::array<bool, 256> held = {};
      std::string typed;
    } keyboard;
  };

  class Machine
  {
  private:
    State _state;
    Memory _memory;
#if SOUND_ENABLED
    sfx::APU _sound;
#endif
    gfx::Font _font;
    lua::Code _code;

  private:
    void circHelper(coord_t xc, coord_t yc, coord_t x, coord_t y, color_t col);
    void circFillHelper(coord_t xc, coord_t yc, coord_t x, coord_t y, color_t col);
    int patterned(coord_t x, coord_t y, color_t color);


  public:
    Machine()
#if SOUND_ENABLED
      : _sound(_memory)
#endif
    {
    }

    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;

    void color(color_t color);

    void cls(color_t color);

    void pset(coord_t x, coord_t y, color_t color);
    void fill(coord_t x, coord_t y, color_t color);
    color_t pget(coord_t x, coord_t y);

    void line(coord_t x0, coord_t y0, coord_t x1, coord_t y1, color_t color);
    void rect(coord_t x0, coord_t y0, coord_t x1, coord_t y1, color_t color);
    void rectfill(coord_t x0, coord_t y0, coord_t x1, coord_t y1, color_t color);
    void circ(coord_t x, coord_t y, amount_t r, color_t color);
    void circfill(coord_t x, coord_t y, amount_t r, color_t color, bool inverted = false);

    void pal(color_t c0, color_t c1, palette_index_t index);

    void map(coord_t cx, coord_t cy, coord_t x, coord_t y, amount_t cw, amount_t ch, sprite_flags_t layer);
    bool visible(coord_t& x, coord_t& y, coord_t w, coord_t h, coord_t& x0, coord_t& y0, coord_t& x1, coord_t& y1);

    /* 0x5f5e keeps the low nibble as the bits a write may change and the high nibble as the bits taken from the colour,
       0 or 0xff leaving every bit to the colour */
    uint8_t bitplanes() { const uint8_t mask = _memory.base()[0x5f5e]; return mask == 0xff ? 0 : mask; }
    static void plot(gfx::color_byte_t* row, coord_t x, color_t color, uint8_t mask)
    {
      if (mask)
      {
        const uint8_t write = mask & 0x0f, read = mask >> 4;
        color = color_t((row[x >> 1].get(x & 1) & ~write & 0x0f) | (color & write & read));
      }
      row[x >> 1].set(x & 1, color);
    }
    void spr(index_t idx, coord_t x, coord_t y);
    void spr(index_t idx, coord_t x, coord_t y, float w, float h, bool flipX, bool flipY);
    void sspr(coord_t sx, coord_t sy, coord_t sw, coord_t sh, coord_t dx, coord_t dy, coord_t dw, coord_t dh, bool flipX, bool flipY);

    /* where a print stopped to wait for frames, so it can carry on afterwards */
    struct PrintState
    {
      size_t position = 0;
      int wait = 0;
      coord_t x = 0, y = 0, sx = 0, right = 0, lineHeight = 0;
      int wide = 1, tall = 1;
      color_t color = color_t(0);
      bool custom = false;
    };

    coord_t print(const std::string& string, coord_t x, coord_t y, color_t color, coord_t* bottom = nullptr, PrintState* state = nullptr);

    State& state() { return _state; }

    /* everything a save state needs: memory, the cart's data, the random generator, sound and the cart's Lua state */
    bool saveState(std::string& out);
    bool loadState(const uint8_t* data, size_t size);
    Memory& memory() { return _memory; }
    gfx::Font& font() { return _font; }
    lua::Code& code() { return _code; }
#if SOUND_ENABLED
    sfx::APU& sound() { return _sound; }
#endif
  };
}

extern retro8::Machine *machine;
