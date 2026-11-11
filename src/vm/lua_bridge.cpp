#include "lua_bridge.h"

#include "machine.h"
#include "lua/lua.hpp"
extern "C" {
#include "lua/eris.h"
}
#include "gen/lua_api.h"
#include "io/cart.h"

#include <functional>
#include <iostream>
#include <fstream>
#include <cstring>
#include <cmath>
#include <ctime>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <algorithm>

#pragma warning(push)
#pragma warning(disable: 4244)

using namespace lua;
using namespace retro8;

using real_t = double;

/* a colour argument becomes the pen (both nibbles, the high one is the fillp colour); without one the pen is used */
static color_t drawColor(lua_State* L, int index)
{
  auto* pen = machine->memory().penColor();

  if (lua_gettop(L) >= index && !lua_isnil(L, index))
    pen->value = uint8_t((int)lua_tonumber(L, index) & 0xff);

  return color_t(pen->value);
}

int pset(lua_State* L)
{
  int x = lua_tonumber(L, 1);
  int y = lua_tonumber(L, 2);

  machine->fill(x, y, drawColor(L, 3));

  return 0;
}

int pget(lua_State* L)
{
  int x = lua_tonumber(L, 1);
  int y = lua_tonumber(L, 2);

  /* off the screen gives 0, or the value at 0x5f5b when bit 4 of 0x5f36 is set */
  if (x < 0 || y < 0 || x > 127 || y > 127)
  {
    const uint8_t* base = machine->memory().base();
    lua_pushinteger(L, (base[address::DRAW_FLAGS] & 0x10) ? base[0x5f5b] : 0);
    return 1;
  }

  lua_pushinteger(L, machine->pget(x, y));

  return 1;
}

int color(lua_State* L)
{
  auto* pen = machine->memory().penColor();
  const int previous = pen->value;
  const int c = lua_isnumber(L, 1) ? (int)lua_tonumber(L, 1) : 6;

  pen->value = uint8_t(c & 0xff);

  lua_pushinteger(L, previous);
  return 1;
}

/* line() forgets the last end point, line(x1, y1, [col]) carries on from it */
static bool lineValid = false;
static int lineX = 0, lineY = 0;

int line(lua_State* L)
{
  if (!lua_isnumber(L, 1))
  {
    lineValid = false;
    return 0;
  }

  if (!lua_isnumber(L, 4))
  {
    const int x1 = lua_tonumber(L, 1), y1 = lua_isnumber(L, 2) ? (int)lua_tonumber(L, 2) : 0;
    const color_t color = drawColor(L, 3);
    if (lineValid)
      machine->line(lineX, lineY, x1, y1, color);
    lineX = x1;
    lineY = y1;
    lineValid = true;
    return 0;
  }

  int x0 = lua_tonumber(L, 1);
  int y0 = lua_tonumber(L, 2);
  int x1 = lua_tonumber(L, 3);
  int y1 = lua_tonumber(L, 4);

  machine->line(x0, y0, x1, y1, drawColor(L, 5));
  lineX = x1;
  lineY = y1;
  lineValid = true;

  return 0;
}

/* fillp(p): the 16 pattern bits are the integer part, 0x0.8 makes set bits transparent */
int fillp(lua_State* L)
{
  uint8_t* fill = machine->memory().base() + retro8::address::FILL_PATTERN;
  const double previous = (int16_t)(fill[0] | (fill[1] << 8)) + ((fill[2] & 1) ? 0.5 : 0.0);
  const double value = lua_isnumber(L, 1) ? lua_tonumber(L, 1) : 0.0;
  const double whole = std::floor(value);
  const int pattern = (int)(int64_t)whole & 0xffff;

  fill[0] = uint8_t(pattern & 0xff);
  fill[1] = uint8_t(pattern >> 8);
  fill[2] = (value - whole) >= 0.5 ? 1 : 0;

  lua_pushnumber(L, previous);
  return 1;
}

int rect(lua_State* L)
{
  int x0 = lua_tonumber(L, 1);
  int y0 = lua_tonumber(L, 2);
  int x1 = lua_tonumber(L, 3);
  int y1 = lua_tonumber(L, 4);

  machine->rect(x0, y0, x1, y1, drawColor(L, 5));

  return 0;
}

// TODO: fill pattern on filled shaped
int rectfill(lua_State* L)
{
  int x0 = lua_tonumber(L, 1);
  int y0 = lua_tonumber(L, 2);
  int x1 = lua_tonumber(L, 3);
  int y1 = lua_tonumber(L, 4);

  machine->rectfill(x0, y0, x1, y1, drawColor(L, 5));

  return 0;
}

int circ(lua_State* L)
{
  int x = lua_tonumber(L, 1);
  int y = lua_tonumber(L, 2);
  int r = lua_gettop(L) >= 3 ? lua_tonumber(L, 3) : 4;

  machine->circ(x, y, r, drawColor(L, 4));

  return 0;
}

int circfill(lua_State* L)
{
  int x = lua_tonumber(L, 1);
  int y = lua_tonumber(L, 2);
  int r = lua_gettop(L) >= 3 ? lua_tonumber(L, 3) : 4;

  /* colour bits 0x1800 draw everything outside the circle when 0x5f34 has bit 1 set */
  const bool inverted = (machine->memory().base()[0x5f34] & 0x02) && lua_isnumber(L, 4) && ((int)(int64_t)lua_tonumber(L, 4) & 0x1800) == 0x1800;

  machine->circfill(x, y, r, drawColor(L, 4), inverted);

  return 0;
}

int cls(lua_State* L)
{
  int c = lua_gettop(L) == 1 ? lua_tonumber(L, -1) : 0;

  machine->cls(color_t(c));

  return 0;
}

int spr(lua_State* L)
{
  const int args = lua_gettop(L);
  const int idx = lua_isnumber(L, 1) ? (int)lua_tonumber(L, 1) : 0;
  const int x = lua_isnumber(L, 2) ? (int)std::floor(lua_tonumber(L, 2)) : 0;
  const int y = lua_isnumber(L, 3) ? (int)std::floor(lua_tonumber(L, 3)) : 0;

  if (idx < 0 || idx > 255)
    return 0;

  if (args > 3)
  {
    const real_t w = lua_isnumber(L, 4) ? lua_tonumber(L, 4) : 1.0;
    const real_t h = lua_isnumber(L, 5) ? lua_tonumber(L, 5) : 1.0;
    const bool fx = lua_toboolean(L, 6);
    const bool fy = lua_toboolean(L, 7);

    machine->spr(idx, x, y, w, h, fx, fy);
  }
  else
    machine->spr(idx, x, y);

  return 0;
}

int sget(lua_State* L)
{
  const int x = lua_isnumber(L, 1) ? (int)lua_tonumber(L, 1) : 0;
  const int y = lua_isnumber(L, 2) ? (int)lua_tonumber(L, 2) : 0;

  /* outside the sheet gives 0, or the value at 0x5f59 when bit 4 of 0x5f36 is set */
  if (x < 0 || y < 0 || x >= 128 || y >= 128)
  {
    uint8_t* base = machine->memory().base();
    lua_pushinteger(L, (base[address::DRAW_FLAGS] & 0x10) ? base[address::SGET_DEFAULT] : 0);
  }
  else
    lua_pushinteger(L, machine->memory().spriteSheet(x, y)->get(x));

  return 1;
}

int sset(lua_State* L)
{
  const int x = lua_isnumber(L, 1) ? (int)lua_tonumber(L, 1) : 0;
  const int y = lua_isnumber(L, 2) ? (int)lua_tonumber(L, 2) : 0;
  color_t c = lua_gettop(L) >= 3 ? color_t((int)lua_tonumber(L, 3) & 0x0f) : machine->memory().penColor()->low();

  if (x >= 0 && y >= 0 && x < 128 && y < 128)
    machine->memory().spriteSheet(x, y)->set(x, c);

  return 0;
}

/* tline draws a line of map pixels, stepping (mdx, mdy) tiles per pixel */
/* PICO-8 lets carts index a string to read one character, s[i] */
static int string_index(lua_State* L)
{
  if (!lua_isnumber(L, 2))
    return 0;

  size_t length = 0;
  const char* text = lua_tolstring(L, 1, &length);
  const lua_Integer i = (lua_Integer)std::floor(lua_tonumber(L, 2));
  if (i < 1 || i > (lua_Integer)length)
    return 0;

  lua_pushlstring(L, text + i - 1, 1);
  return 1;
}

static int set_fps(lua_State* L)
{
  machine->code().setFps(lua_isnumber(L, 1) ? (int)lua_tonumber(L, 1) : 30);
  return 0;
}

/* tline(n) sets how many fraction bits the map coordinates carry, 13 by default for tiles and 16 for pixels */
static int tlineBits = 13;

/* tline draws a line of map pixels, stepping (mdx, mdy) per pixel; 0x5f38 and 0x5f39 wrap the map coordinates by masking them
 * with the value less 0x0.0001, and 0x5f3a and 0x5f3b then offset them, all in tiles */
int tline(lua_State* L)
{
  if (lua_gettop(L) == 1)
  {
    tlineBits = (int)lua_tonumber(L, 1);
    return 0;
  }

  const double scale = std::ldexp(1.0, 13 - tlineBits);
  const int x0 = (int)std::floor(luaL_optnumber(L, 1, 0));
  const int y0 = (int)std::floor(luaL_optnumber(L, 2, 0));
  const int x1 = (int)std::floor(luaL_optnumber(L, 3, 0));
  const int y1 = (int)std::floor(luaL_optnumber(L, 4, 0));
  double mx = luaL_optnumber(L, 5, 0) * scale;
  double my = luaL_optnumber(L, 6, 0) * scale;
  const double mdx = lua_isnumber(L, 7) ? lua_tonumber(L, 7) * scale : 0.125;
  const double mdy = lua_isnumber(L, 8) ? lua_tonumber(L, 8) * scale : 0;
  const int layers = (int)luaL_optnumber(L, 9, 0);

  const uint8_t* hw = machine->memory().base();
  const uint32_t maskX = (uint32_t(hw[0x5f38]) << 16) - 1, maskY = (uint32_t(hw[0x5f39]) << 16) - 1;
  const int offsetX = hw[0x5f3a], offsetY = hw[0x5f3b];

  const gfx::palette_t* palette = machine->memory().paletteAt(gfx::DRAW_PALETTE_INDEX);
  const int dx = x1 - x0, dy = y1 - y0;
  const int steps = std::max(std::abs(dx), std::abs(dy));

  for (int i = 0; i <= steps; ++i)
  {
    const int x = steps ? x0 + (int)std::lround((double)dx * i / steps) : x0;
    const int y = steps ? y0 + (int)std::lround((double)dy * i / steps) : y0;
    const uint32_t bx = (uint32_t)(int64_t)std::floor(mx * 65536.0) & maskX;
    const uint32_t by = (uint32_t)(int64_t)std::floor(my * 65536.0) & maskY;
    const int tx = (int)((int32_t)bx >> 16) + offsetX, ty = (int)((int32_t)by >> 16) + offsetY;

    if (machine->memory().inMap(tx, ty))
    {
      const sprite_index_t sprite = *machine->memory().spriteInTileMap(tx, ty);
      if (sprite != 0 && (!layers || (layers & *machine->memory().spriteFlagsFor(sprite)) != 0))
      {
        const int sx = (sprite % 16) * 8 + (int)((bx >> 13) & 7);
        const int sy = (sprite / 16) * 8 + (int)((by >> 13) & 7);
        const color_t color = machine->memory().spriteSheet(sx, sy)->get(sx);
        if (!palette->transparent(color))
          machine->pset(x, y, color);
      }
    }

    mx += mdx;
    my += mdy;
  }

  return 0;
}


static int pal_set(int c0, int c1, palette_index_t index)
{
  /* p = 2 is the secondary palette at 0x5f60 that 0x5f5f shows on chosen lines, stored like the screen palette */
  if (index == 2)
  {
    uint8_t& entry = machine->memory().base()[0x5f60 + (c0 & 0x0f)];
    const int previous = entry;
    entry = uint8_t(c1);
    return previous;
  }
  if (index > gfx::SCREEN_PALETTE_INDEX)
    return 0;

  gfx::palette_t* palette = machine->memory().paletteAt(index);
  const color_t c = color_t(c0 & 0x0f);
  if (index == gfx::SCREEN_PALETTE_INDEX)
  {
    const int previous = palette->raw(c) & 0x8f;
    palette->setScreen(c, uint8_t(c1));
    return previous;
  }
  const int previous = palette->get(c);
  machine->pal(c, static_cast<color_t>(c1 & 0x0f), index);
  return previous;
}

int pal(lua_State* L)
{
  const int args = lua_gettop(L);

  if (args == 0 || lua_isnil(L, 1))
  {
    machine->memory().paletteAt(gfx::DRAW_PALETTE_INDEX)->reset();
    machine->memory().paletteAt(gfx::SCREEN_PALETTE_INDEX)->resetScreen();
    memset(machine->memory().base() + 0x5f60, 0, 16);
    return 0;
  }

  if (args == 1 && lua_isnumber(L, 1))
  {
    const int index = (int)lua_tonumber(L, 1);
    if (index == (int)gfx::DRAW_PALETTE_INDEX)
      machine->memory().paletteAt(gfx::DRAW_PALETTE_INDEX)->reset();
    else if (index == (int)gfx::SCREEN_PALETTE_INDEX)
      machine->memory().paletteAt(gfx::SCREEN_PALETTE_INDEX)->resetScreen();
    else if (index == 2)
      memset(machine->memory().base() + 0x5f60, 0, 16);
    return 0;
  }

  /* pal(table, p) remaps every colour listed in the table */
  if (lua_istable(L, 1))
  {
    const palette_index_t index = lua_isnumber(L, 2) ? (palette_index_t)lua_tonumber(L, 2) : gfx::DRAW_PALETTE_INDEX;
    lua_pushnil(L);
    while (lua_next(L, 1))
    {
      if (lua_isnumber(L, -2) && lua_isnumber(L, -1))
        pal_set((int)lua_tonumber(L, -2), (int)lua_tonumber(L, -1), index);
      lua_pop(L, 1);
    }
    return 0;
  }

  const int c0 = (int)lua_tonumber(L, 1);
  const int c1 = lua_isnumber(L, 2) ? (int)lua_tonumber(L, 2) : c0;
  const palette_index_t index = lua_isnumber(L, 3) ? (palette_index_t)lua_tonumber(L, 3) : gfx::DRAW_PALETTE_INDEX;

  lua_pushinteger(L, pal_set(c0, c1, index));
  return 1;
}

/* palt(c, t) sets one colour and returns its old setting, palt(bits) sets all 16 with bit 15 for colour 0 and returns the old bits */
int palt(lua_State* L)
{
  gfx::palette_t* palette = machine->memory().paletteAt(gfx::DRAW_PALETTE_INDEX);
  int previous = 0;
  for (int i = 0; i < 16; ++i)
    if (palette->transparent(color_t(i)))
      previous |= 0x8000 >> i;

  if (lua_gettop(L) == 0 || lua_isnil(L, 1))
  {
    palette->resetTransparency();
    lua_pushinteger(L, previous);
  }
  else if (lua_gettop(L) == 1)
  {
    const int bits = (int)(int64_t)lua_tonumber(L, 1);
    for (int i = 0; i < 16; ++i)
      palette->transparent(color_t(i), (bits & (0x8000 >> i)) != 0);
    lua_pushinteger(L, previous);
  }
  else
  {
    const color_t c = color_t(int(lua_tonumber(L, 1)) & 0x0f);
    lua_pushboolean(L, palette->transparent(c));
    palette->transparent(c, lua_toboolean(L, 2));
  }
  return 1;
}

namespace draw
{
  int clip(lua_State* L)
  {
    auto* rect = machine->memory().clipRect();
    const int px = rect->x0, py = rect->y0, pw = rect->x1 - rect->x0, ph = rect->y1 - rect->y0;

    if (!lua_isnumber(L, 1))
      rect->reset();
    else
    {
      int x0 = (int)lua_tonumber(L, 1), y0 = (int)luaL_optnumber(L, 2, 0);
      int x1 = x0 + (int)luaL_optnumber(L, 3, 128), y1 = y0 + (int)luaL_optnumber(L, 4, 128);

      if (lua_toboolean(L, 5))
      {
        x0 = std::max<int>(x0, rect->x0);
        y0 = std::max<int>(y0, rect->y0);
        x1 = std::min<int>(x1, rect->x1);
        y1 = std::min<int>(y1, rect->y1);
      }

      x0 = std::max(0, std::min(x0, (int)gfx::SCREEN_WIDTH));
      y0 = std::max(0, std::min(y0, (int)gfx::SCREEN_HEIGHT));
      x1 = std::max(x0, std::min(x1, (int)gfx::SCREEN_WIDTH));
      y1 = std::max(y0, std::min(y1, (int)gfx::SCREEN_HEIGHT));
      rect->set(x0, y0, x1, y1);
    }

    lua_pushinteger(L, px);
    lua_pushinteger(L, py);
    lua_pushinteger(L, pw);
    lua_pushinteger(L, ph);
    return 4;
  }
}



int camera(lua_State* L)
{
  auto* camera = machine->memory().camera();
  const int px = camera->x(), py = camera->y();

  const int16_t cx = lua_isnumber(L, 1) ? (int16_t)std::floor(lua_tonumber(L, 1)) : 0;
  const int16_t cy = lua_isnumber(L, 2) ? (int16_t)std::floor(lua_tonumber(L, 2)) : 0;
  camera->set(cx, cy);

  lua_pushinteger(L, px);
  lua_pushinteger(L, py);
  return 2;
}

int map(lua_State* L)
{
  coord_t cx = lua_tonumber(L, 1);
  coord_t cy = lua_tonumber(L, 2);
  coord_t x = lua_tonumber(L, 3);
  coord_t y = lua_tonumber(L, 4);
  amount_t cw = lua_gettop(L) >= 5 ? lua_tonumber(L, 5) : machine->memory().mapWidth();
  amount_t ch = lua_gettop(L) >= 6 ? lua_tonumber(L, 6) : machine->memory().mapHeight();
  sprite_flags_t layer = 0;

  if (lua_gettop(L) == 7)
    layer = lua_tonumber(L, 7);

  machine->map(cx, cy, x, y, cw, ch, layer);

  return 0;
}

int mget(lua_State* L)
{
  int x = lua_tonumber(L, 1); //TODO: these are optional
  int y = lua_tonumber(L, 2);

  /* outside the map gives 0, or the value at 0x5f5a when bit 4 of 0x5f36 is set */
  uint8_t* base = machine->memory().base();
  sprite_index_t index = (base[address::DRAW_FLAGS] & 0x10) ? base[address::MGET_DEFAULT] : 0;

  if (machine->memory().inMap(x, y))
    index = *machine->memory().spriteInTileMap(x, y);

  //printf("mget(%d, %d) = %d\n", x, y, index);

  lua_pushnumber(L, index);

  return 1;
}

int mset(lua_State* L)
{
  int x = lua_tonumber(L, 1);
  int y = lua_tonumber(L, 2);
  retro8::sprite_index_t index = lua_tonumber(L, 3);

  if (machine->memory().inMap(x, y))
    *machine->memory().spriteInTileMap(x, y) = index;

  return 0;
}

static std::string print_text(lua_State* L)
{
  switch (lua_type(L, 1))
  {
  case LUA_TSTRING:
  case LUA_TNUMBER: return lua_tostring(L, 1);
  case LUA_TBOOLEAN: return lua_toboolean(L, 1) ? "true" : "false";
  case LUA_TTABLE: return "[table]";
  case LUA_TFUNCTION: return "[function]";
  default: return "[nil]";
  }
}

static int print_step(lua_State* L);

static int print_continue(lua_State* L, int status, lua_KContext ctx)
{
  return print_step(L);
}

/* the text and its PrintState sit on the stack, so a print that waits can carry on after the yield */
static int print_step(lua_State* L)
{
  size_t length = 0;
  const char* text = lua_tolstring(L, 1, &length);
  auto* state = static_cast<Machine::PrintState*>(lua_touserdata(L, 2));
  const bool canWait = machine->code().isCartThread(L) && lua_isyieldable(L);
  retro8::coord_t bottom = 0;

  const retro8::coord_t right = machine->print(std::string(text, length), state->x, state->y, state->color, &bottom, canWait ? state : nullptr);

  if (canWait && state->wait > 0)
  {
    machine->code().waitFrames(state->wait - 1);
    return lua_yieldk(L, 0, 0, print_continue);
  }

  lua_pushinteger(L, right);
  lua_pushinteger(L, bottom);
  return 2;
}

/* PICO-8 returns the right-most x the text reached, which carts use to centre it */
int print(lua_State* L)
{
  const std::string text = print_text(L);
  const int args = lua_gettop(L);
  auto* cursor = machine->memory().cursor();
  retro8::color_t c = machine->memory().penColor()->low();
  int x, y;

  if (args >= 3)
  {
    x = lua_tonumber(L, 2);
    y = lua_tonumber(L, 3);
    if (args >= 4 && lua_isnumber(L, 4))
      c = static_cast<retro8::color_t>((int)lua_tonumber(L, 4) & 0x0f);
  }
  else
  {
    x = cursor->x();
    y = cursor->y();
    if (args == 2 && lua_isnumber(L, 2))
      c = static_cast<retro8::color_t>((int)lua_tonumber(L, 2) & 0x0f);
  }

  machine->memory().penColor()->low(c);
  cursor->set(x, y + TEXT_LINE_HEIGHT);

  lua_settop(L, 0);
  lua_pushlstring(L, text.data(), text.size());
  auto* state = static_cast<Machine::PrintState*>(lua_newuserdata(L, sizeof(Machine::PrintState)));
  new (state) Machine::PrintState();

  /* plain data, so a save state taken while a print waits can copy it as it is */
  if (lua_getfield(L, LUA_REGISTRYINDEX, "retro8.printstate") != LUA_TTABLE)
  {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "__persist");
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, "retro8.printstate");
  }
  lua_setmetatable(L, -2);
  state->x = x;
  state->y = y;
  state->color = c;

  return print_step(L);
}

int cursor(lua_State* L)
{
  auto* cursor = machine->memory().cursor();
  const int px = cursor->x(), py = cursor->y(), pc = machine->memory().penColor()->low();

  const int x = lua_isnumber(L, 1) ? (int)lua_tonumber(L, 1) : 0;
  const int y = lua_isnumber(L, 2) ? (int)lua_tonumber(L, 2) : 0;
  *cursor = { (uint8_t)x, (uint8_t)y };

  if (lua_isnumber(L, 3))
    machine->memory().penColor()->low(static_cast<retro8::color_t>((int)lua_tonumber(L, 3) & 0x0f));

  lua_pushinteger(L, px);
  lua_pushinteger(L, py);
  lua_pushinteger(L, pc);
  return 3;
}

namespace debug
{
  int debugprint(lua_State* L)
  {
    std::string text = lua_tostring(L, 1);
    std::cout << text << std::endl;

    return 0;
  }

  int breakpoint(lua_State* L)
  {
#if _WIN32
    __debugbreak();
#endif
    return 0;
  }
}


namespace sprites
{
  int fget(lua_State* L)
  {
    retro8::sprite_index_t index = lua_tonumber(L, 1);
    retro8::sprite_flags_t flags = *machine->memory().spriteFlagsFor(index);

    if (lua_gettop(L) == 2)
    {
      int index = lua_tonumber(L, 2);
      assert(index >= 0 && index <= 7);
      lua_pushboolean(L, (flags >> index) & 0x1 ? true : false);
    }
    else
      lua_pushnumber(L, *machine->memory().spriteFlagsFor(index));

    return 1;
  }

  int fset(lua_State* L)
  {
    retro8::sprite_index_t index = lua_tonumber(L, 1);
    retro8::sprite_flags_t* flags = machine->memory().spriteFlagsFor(index);

    if (lua_gettop(L) == 3)
    {
      int index = lua_tonumber(L, 2);
      bool value = lua_toboolean(L, 3);
      assert(index >= 0 && index <= 7);

      if (value)
        *flags = *flags | (1 << index);
      else
        *flags = *flags & ~(1 << index);
    }
    else
    {
      retro8::sprite_flags_t value = lua_tonumber(L, 2);
      *flags = value;
    }

    return 0;
  }

  int sspr(lua_State* L)
  {
    coord_t sx = lua_tonumber(L, 1);
    coord_t sy = lua_tonumber(L, 2);
    coord_t sw = lua_tonumber(L, 3);
    coord_t sh = lua_tonumber(L, 4);
    coord_t dx = lua_tonumber(L, 5);
    coord_t dy = lua_tonumber(L, 6);
    coord_t dw = lua_to_or_default(L, number, 7, sw);
    coord_t dh = lua_to_or_default(L, number, 8, sh);
    bool flipX = lua_toboolean(L, 9);
    bool flipY = lua_toboolean(L, 10);

    machine->sspr(sx, sy, sw, sh, dx, dy, dw, dh, flipX, flipY);

    return 0;
  }
}

namespace math
{
  using real_t = double;
  static constexpr real_t PI = 3.14159265358979323846;

  /* PICO-8 maths results are 16.16 numbers, so sin(0.25) is exactly -1 */
  static inline real_t fixed(real_t value) { return std::floor(value * 65536.0 + 0.5) / 65536.0; }

  int cos(lua_State* L)
  {
    if (lua_isnumber(L, 1))
    {
      real_t angle = lua_tonumber(L, 1);
      real_t value = std::cos(angle * 2 * PI);
      lua_pushnumber(L, fixed(value));
    }
    else
      lua_pushnumber(L, 0);

    return 1;
  }

  int sin(lua_State* L)
  {
    if (lua_isnumber(L, 1))
    {
      real_t angle = lua_tonumber(L, 1);
      real_t value = std::sin(-angle * 2 * PI);
      lua_pushnumber(L, fixed(value));
    }
    else
      lua_pushnumber(L, 0);

    return 1;
  }

  int atan2(lua_State* L)
  {
    assert(lua_isnumber(L, 1));
    real_t dx = lua_tonumber(L, 1);
    real_t dy = lua_tonumber(L, 2);
    real_t value = std::atan2(dx, dy) / (2 * PI) - 0.25;
    if (value < 0.0)
      value += 1.0;

    lua_pushnumber(L, fixed(value) >= 1.0 ? 0.0 : fixed(value));

    return 1;
  }

  int srand(lua_State* L)
  {
    real_t seed = lua_isnumber(L, 1) ? lua_tonumber(L, 1) : 0;
    machine->state().rnd.seed(seed);

    return 0;
  }

#define FAIL_IF_NOT_NUMBER(i) do { if (!lua_isnumber(L, i)) { printf("Expected number but got %s\n", lua_typename(L, i)); assert(false); } } while (false)

  /* rnd(x) is in [0, x), and rnd(table) picks one of its elements */
  int rnd(lua_State* L)
  {
    const double unit = machine->state().rnd() / ((double)machine->state().rnd.max() + 1.0);

    if (lua_istable(L, 1))
    {
      const lua_Integer count = (lua_Integer)lua_rawlen(L, 1);
      if (count <= 0)
        return 0;
      lua_rawgeti(L, 1, 1 + (lua_Integer)(unit * (double)count));
      return 1;
    }

    const real_t max = lua_isnumber(L, 1) ? lua_tonumber(L, 1) : 1.0;
    lua_pushnumber(L, unit * max);

    return 1;
  }

  /* whole results go back as integers, so using them as table keys needs no conversion */
  static void push_whole(lua_State* L, double value)
  {
    if (value >= -2147483648.0 && value < 2147483648.0)
      lua_pushinteger(L, (lua_Integer)value);
    else
      lua_pushnumber(L, value);
  }

  int flr(lua_State* L)
  {
    if (lua_isinteger(L, 1))
    {
      lua_settop(L, 1);
      return 1;
    }
    push_whole(L, std::floor(lua_isnumber(L, 1) ? lua_tonumber(L, 1) : 0));
    return 1;
  }

  int ceil(lua_State* L)
  {
    if (lua_isinteger(L, 1))
    {
      lua_settop(L, 1);
      return 1;
    }
    push_whole(L, std::ceil(lua_isnumber(L, 1) ? lua_tonumber(L, 1) : 0));
    return 1;
  }


  int min(lua_State* L)
  {
    real_t v1 = lua_isnumber(L, 1) ? lua_tonumber(L, 1) : 0;
    real_t v2 = 0;

    if (lua_gettop(L) == 2 && lua_isnumber(L, 2))
      v2 = lua_tonumber(L, 2);

    lua_pushnumber(L, std::min(v1, v2));

    return 1;
  }

  int max(lua_State* L)
  {
    real_t v1 = lua_isnumber(L, 1) ? lua_tonumber(L, 1) : 0;
    real_t v2 = 0;

    if (lua_gettop(L) == 2 && lua_isnumber(L, 2))
    {
      FAIL_IF_NOT_NUMBER(2);
      v2 = lua_tonumber(L, 2);
    }

    lua_pushnumber(L, std::max(v1, v2));

    return 1;
  }

  int mid(lua_State* L)
  {
    real_t a = lua_tonumber(L, 1);
    real_t b = lua_tonumber(L, 2);
    real_t c = lua_gettop(L) >= 3 ? lua_tonumber(L, 3) : 0;

    if ((a <= b && b <= c) || (c <= b && b <= a))
      lua_pushnumber(L, b);
    else if ((b <= a && a <= c) || (c <= a && a <= b))
      lua_pushnumber(L, a);
    else
      lua_pushnumber(L, c);

    return 1;
  }

  int abs(lua_State* L)
  {
    if (lua_isnumber(L, 1))
    {
      real_t v = lua_tonumber(L, 1);
      lua_pushnumber(L, std::abs(v));
    }
    else
      lua_pushnumber(L, 0);

    return 1;
  }

  int sgn(lua_State* L)
  {
    assert(lua_isnumber(L, 1));

    real_t v = lua_tonumber(L, 1);
    lua_pushnumber(L, v > 0 ? 1.0 : -1.0);

    return 1;
  }

  int sqrt(lua_State* L)
  {
    assert(lua_isnumber(L, 1));

    /* PICO-8 gives 0 for a negative number */
    real_t v = lua_tonumber(L, 1);
    lua_pushnumber(L, v > 0 ? fixed(std::sqrt(v)) : 0);

    return 1;
  }
}

#define EXPECT_TYPE(tn, idx) do { if (!lua_is ## tn(L, idx)) std::cout << "expected " # tn << " but got " << lua_typename(L, idx) << std::endl; assert(false);} while (false)

namespace bitwise
{
  /* the operators and these functions share Lua's PICO-8 16.16 fixed point bit maths */
  int arith(lua_State* L, int op)
  {
    /* a missing or non-number argument counts as 0, so bor(nil, x) is x */
    const lua_Number a = lua_isnumber(L, 1) ? lua_tonumber(L, 1) : 0;
    const lua_Number b = lua_isnumber(L, 2) ? lua_tonumber(L, 2) : 0;

    lua_pushnumber(L, a);
    if (op != LUA_OPBNOT)
      lua_pushnumber(L, b);
    lua_arith(L, op);

    return 1;
  }

  inline int band(lua_State* L) { return arith(L, LUA_OPBAND); }
  inline int bor(lua_State* L) { return arith(L, LUA_OPBOR); }
  inline int bxor(lua_State* L) { return arith(L, LUA_OPBXOR); }
  inline int bnot(lua_State* L) { return arith(L, LUA_OPBNOT); }

  inline int shl(lua_State* L) { return arith(L, LUA_OPSHL); }
  inline int shr(lua_State* L) { return arith(L, LUA_OPSHR); }

  inline int lshl(lua_State* L) { return arith(L, LUA_OPSHL); }
  inline int lshr(lua_State* L) { return arith(L, LUA_OPLSHR); }

  inline int rotl(lua_State* L) { return arith(L, LUA_OPROTL); }
  inline int rotr(lua_State* L) { return arith(L, LUA_OPROTR); }
}

namespace sound
{
  int music(lua_State* L)
  {
#if SOUND_ENABLED
    sfx::music_index_t index = lua_isnumber(L, 1) ? (sfx::music_index_t)lua_tonumber(L, 1) : 0;
    int32_t fadeMs = lua_isnumber(L, 2) ? (int32_t)lua_tonumber(L, 2) : 0;
    int32_t mask = lua_isnumber(L, 3) ? (int32_t)lua_tonumber(L, 3) : 0;

    machine->sound().music(index, fadeMs, mask);
#endif

    return 0;
  }

  int sfx(lua_State* L)
  {
#if SOUND_ENABLED
    const sfx::sound_index_t index = lua_isnumber(L, 1) ? (sfx::sound_index_t)lua_tonumber(L, 1) : -1;
    const sfx::channel_index_t channel = lua_isnumber(L, 2) ? (sfx::channel_index_t)lua_tonumber(L, 2) : -1;

    /* -1 stops and -2 releases a loop, any other number outside the 64 sounds does nothing */
    if (index < 0)
    {
      if (index == -1 || index == -2)
        machine->sound().play(index, channel, 0, 0);
      return 0;
    }
    if (index >= (sfx::sound_index_t)sfx::SOUND_COUNT)
      return 0;

    const int32_t start = std::max(0, lua_isnumber(L, 3) ? (int32_t)lua_tonumber(L, 3) : 0);
    const int32_t length = lua_isnumber(L, 4) ? (int32_t)lua_tonumber(L, 4) : -1;

    machine->sound().play(index, channel, start, length);
#endif

    return 0;
  }
}

namespace string
{
  int sub(lua_State* L)
  {
    size_t length = 0;
    const char* text = lua_isstring(L, 1) ? lua_tolstring(L, 1, &length) : nullptr;

    if (!text)
    {
      lua_pushstring(L, "");
      return 1;
    }

    const lua_Integer len = (lua_Integer)length;
    lua_Integer first = lua_isnumber(L, 2) ? (lua_Integer)std::floor(lua_tonumber(L, 2)) : 1;
    lua_Integer last = lua_isnumber(L, 3) ? (lua_Integer)std::floor(lua_tonumber(L, 3)) : -1;

    if (first < 0) first = std::max<lua_Integer>(len + first + 1, 1);
    else if (first == 0) first = 1;
    if (last < 0) last = len + last + 1;
    else if (last > len) last = len;

    if (first > last)
      lua_pushstring(L, "");
    else
      lua_pushlstring(L, text + first - 1, (size_t)(last - first + 1));

    return 1;
  }

  int tostr(lua_State* L)
  {
    char buffer[32];

    switch (lua_type(L, 1))
    {
    case LUA_TNONE:
    case LUA_TNIL: lua_pushstring(L, "[nil]"); break;
    case LUA_TBOOLEAN: lua_pushstring(L, lua_toboolean(L, 1) ? "true" : "false"); break;
    case LUA_TNUMBER:
    {
      /* flag 1 shows the 16.16 bits in hex, flag 2 shows them as a whole number, true is the same as flag 1 */
      const double value = lua_tonumber(L, 1);
      const int flags = lua_isnumber(L, 2) ? (int)lua_tonumber(L, 2) : (lua_toboolean(L, 2) ? 1 : 0);
      const uint32_t bits = (uint32_t)(int64_t)std::floor(value * 65536.0);

      if ((flags & 3) == 3)
        snprintf(buffer, sizeof(buffer), "0x%08x", (unsigned)bits);
      else if (flags & 2)
        snprintf(buffer, sizeof(buffer), "%d", (int32_t)bits);
      else if (flags & 1)
        snprintf(buffer, sizeof(buffer), "0x%04x.%04x", (unsigned)(bits >> 16), (unsigned)(bits & 0xffff));
      else
      {
        lua_pushvalue(L, 1);
        lua_tostring(L, -1);
        break;
      }

      lua_pushstring(L, buffer);
      break;
    }
    case LUA_TSTRING: lua_pushvalue(L, 1); break;
    case LUA_TTABLE: lua_pushstring(L, "[table]"); break;
    case LUA_TFUNCTION: lua_pushstring(L, "[function]"); break;
    case LUA_TTHREAD: lua_pushstring(L, "[thread]"); break;
    default: lua_pushstring(L, "[userdata]");
    }

    return 1;
  }

  int tonum(lua_State* L)
  {
    const int flags = lua_isnumber(L, 2) ? (int)lua_tonumber(L, 2) : 0;

    if (lua_type(L, 1) == LUA_TNUMBER)
    {
      lua_pushvalue(L, 1);
      return 1;
    }

    if (lua_type(L, 1) == LUA_TBOOLEAN)
    {
      lua_pushnumber(L, lua_toboolean(L, 1) ? 1 : 0);
      return 1;
    }

    if (lua_type(L, 1) == LUA_TSTRING)
    {
      /* PICO-8 does not skip whitespace, so " 5" and "1\n" are not numbers */
      std::string text = lua_tostring(L, 1);
      if (text.find_first_of(" \t\n\r") != std::string::npos)
        text.clear();

      /* flag 0x1 reads the text as hex without a prefix */
      if ((flags & 1) && !text.empty())
      {
        const bool negative = text[0] == '-';
        text = (negative ? "-0x" : "0x") + text.substr(negative ? 1 : 0);
      }

      if (!text.empty() && lua_stringtonumber(L, text.c_str()) != 0)
      {
        /* flag 0x2 reads the value as raw 16.16 bits */
        if (flags & 2)
        {
          const double raw = lua_tonumber(L, -1);
          lua_pop(L, 1);
          lua_pushnumber(L, (int32_t)(uint32_t)(int64_t)raw / 65536.0);
        }
        return 1;
      }
    }

    /* flag 0x4 asks for 0 instead of nothing when the conversion fails */
    if (flags & 4)
    {
      lua_pushnumber(L, 0);
      return 1;
    }

    return 0;
  }
}

namespace platform
{
  /* PICO-8 has 64K of addressable memory and every address wraps inside it */
  static constexpr int32_t MEMORY_SIZE = retro8::Memory::MEMORY_SIZE;

  static inline uint8_t* mem() { return machine->memory().base(); }

  static inline int32_t address_at(lua_State* L, int index)
  {
    return lua_isnumber(L, index) ? (int32_t)((int64_t)std::floor(lua_tonumber(L, index)) & 0xffff) : 0;
  }

  static inline int32_t int_at(lua_State* L, int index)
  {
    return lua_isnumber(L, index) ? (int32_t)(int64_t)std::floor(lua_tonumber(L, index)) : 0;
  }

  static inline int32_t fixed_at(lua_State* L, int index)
  {
    return lua_isnumber(L, index) ? (int32_t)(uint32_t)(int64_t)std::floor(lua_tonumber(L, index) * 65536.0) : 0;
  }

  static inline uint8_t read8(int32_t addr) { return mem()[machine->memory().dataAddress(addr)]; }
  static inline void write8(int32_t addr, uint8_t value) { mem()[machine->memory().dataAddress(addr)] = value; }

  static int count_at(lua_State* L, int index)
  {
    int count = lua_isnumber(L, index) ? (int)lua_tonumber(L, index) : 1;
    if (count < 0) count = 0;
    if (count > 8192) count = 8192;
    return count;
  }

  int poke(lua_State* L)
  {
    const int32_t addr = address_at(L, 1);
    const int values = lua_gettop(L) - 1;
    for (int i = 0; i < values; ++i)
      write8(addr + i, (uint8_t)int_at(L, 2 + i));

    return 0;
  }

  int poke2(lua_State* L)
  {
    const int32_t addr = address_at(L, 1);
    const int values = lua_gettop(L) - 1;
    for (int i = 0; i < values; ++i)
    {
      const uint16_t value = (uint16_t)int_at(L, 2 + i);
      write8(addr + i * 2, value & 0xff);
      write8(addr + i * 2 + 1, value >> 8);
    }

    return 0;
  }

  int poke4(lua_State* L)
  {
    const int32_t addr = address_at(L, 1);
    const int values = lua_gettop(L) - 1;
    for (int i = 0; i < values; ++i)
    {
      const uint32_t value = (uint32_t)fixed_at(L, 2 + i);
      for (int b = 0; b < 4; ++b)
        write8(addr + i * 4 + b, (value >> (b * 8)) & 0xff);
    }

    return 0;
  }

  int peek(lua_State* L)
  {
    const int32_t addr = address_at(L, 1);
    const int count = count_at(L, 2);
    luaL_checkstack(L, count, "too many peek results");
    for (int i = 0; i < count; ++i)
      lua_pushinteger(L, read8(addr + i));

    return count;
  }

  int peek2(lua_State* L)
  {
    const int32_t addr = address_at(L, 1);
    const int count = count_at(L, 2);
    luaL_checkstack(L, count, "too many peek2 results");
    for (int i = 0; i < count; ++i)
      lua_pushinteger(L, (int16_t)(read8(addr + i * 2) | (read8(addr + i * 2 + 1) << 8)));

    return count;
  }

  int peek4(lua_State* L)
  {
    const int32_t addr = address_at(L, 1);
    const int count = count_at(L, 2);
    luaL_checkstack(L, count, "too many peek4 results");
    for (int i = 0; i < count; ++i)
    {
      uint32_t value = 0;
      for (int b = 0; b < 4; ++b)
        value |= (uint32_t)read8(addr + i * 4 + b) << (b * 8);
      const int32_t fixed = (int32_t)value;
      if ((fixed & 0xffff) == 0)
        lua_pushinteger(L, fixed >> 16);
      else
        lua_pushnumber(L, fixed / 65536.0);
    }

    return count;
  }

  int memset(lua_State* L)
  {
    const int32_t addr = address_at(L, 1);
    const uint8_t value = (uint8_t)int_at(L, 2);
    const int32_t length = std::max(0, std::min(int_at(L, 3), MEMORY_SIZE));

    if (length > 0 && machine->memory().identityMapping() && addr + length <= MEMORY_SIZE)
      std::memset(mem() + addr, value, length);
    else
      for (int32_t i = 0; i < length; ++i)
        write8(addr + i, value);

    return 0;
  }

  int memcpy(lua_State* L)
  {
    const int32_t dest = address_at(L, 1);
    const int32_t src = address_at(L, 2);
    const int32_t length = std::max(0, std::min(int_at(L, 3), MEMORY_SIZE));

    if (length > 0 && machine->memory().identityMapping() && dest + length <= MEMORY_SIZE && src + length <= MEMORY_SIZE)
      std::memmove(mem() + dest, mem() + src, length);
    else if (length > 0)
    {
      static std::vector<uint8_t> copied;
      copied.resize(length);
      for (int32_t i = 0; i < length; ++i)
        copied[i] = read8(src + i);
      for (int32_t i = 0; i < length; ++i)
        write8(dest + i, copied[i]);
    }

    return 0;
  }

  int reload(lua_State* L)
  {
    /* addresses are unsigned 16 bits, so 0x8000, which reads as -32768, is the upper half of memory */
    int32_t dest = (int32_t)(lua_to_or_default(L, number, 1, 0)) & 0xffff;
    int32_t src = (int32_t)(lua_to_or_default(L, number, 2, 0)) & 0xffff;
    int32_t length = lua_to_or_default(L, number, 3, address::CART_DATA_LENGTH);

    if (length <= 0)
      return 0;
    length = std::min(length, address::CART_DATA_LENGTH - src);
    length = std::min(length, MEMORY_SIZE - dest);
    if (length <= 0)
      return 0;

    /* a fourth argument reads the data of another cart in the same folder */
    if (lua_isstring(L, 4))
    {
      static uint8_t rom[address::CART_DATA_LENGTH];
      if (retro8::io::cartRom(lua_tostring(L, 4), rom))
        std::memcpy(machine->memory().base() + dest, rom + src, length);
      return 0;
    }

    std::memcpy(machine->memory().base() + dest, machine->memory().backup() + src, length);

    return 0;
  }

  int btn(lua_State* L)
  {
    const index_t index = lua_isnumber(L, 2) ? lua_tonumber(L, 2) : 0;
 
    /* we're asking for a specific button*/
    if (lua_gettop(L) >= 1)
    {
      using bt_t = retro8::button_t;
      static constexpr std::array<bt_t, 6> buttons = { bt_t::LEFT, bt_t::RIGHT, bt_t::UP, bt_t::DOWN, bt_t::ACTION1, bt_t::ACTION2 };
      const int bindex = lua_isnumber(L, 1) ? (int)lua_tonumber(L, 1) : -1;

      if (index < PLAYER_COUNT && bindex >= 0 && bindex < (int)buttons.size())
        lua_pushboolean(L, machine->state().buttons[index].isSet(buttons[bindex]));
      else
        lua_pushboolean(L, false);

    }
    /* push whole bitmask*/
    else
    {
      lua_pushinteger(L, machine->state().buttons[0].value | (machine->state().buttons[1].value << 8));
    }

    return 1;
  }

  int btnp(lua_State* L)
  {
    const index_t index = lua_isnumber(L, 2) ? lua_tonumber(L, 2) : 0;

    /* we're asking for a specific button*/
    if (lua_gettop(L) >= 1)
    {
      using bt_t = retro8::button_t;
      static constexpr std::array<bt_t, 6> buttons = { bt_t::LEFT, bt_t::RIGHT, bt_t::UP, bt_t::DOWN, bt_t::ACTION1, bt_t::ACTION2 };
      const int bindex = lua_isnumber(L, 1) ? (int)lua_tonumber(L, 1) : -1;

      if (index < PLAYER_COUNT && bindex >= 0 && bindex < (int)buttons.size())
        lua_pushboolean(L, machine->state().previousButtons[index].isSet(buttons[bindex]));
      else
        lua_pushboolean(L, false);
    }
    /* push whole bitmask*/
    else
    {
      lua_pushinteger(L, machine->state().previousButtons[0].value | (machine->state().previousButtons[1].value << 8));
    }

    return 1;
  }

  static int channel_sound(int channel)
  {
#if SOUND_ENABLED
    return machine->sound().channelSound(channel);
#else
    return -1;
#endif
  }

  static int channel_note(int channel)
  {
#if SOUND_ENABLED
    return machine->sound().channelNote(channel);
#else
    return -1;
#endif
  }

  static int music_ticks()
  {
#if SOUND_ENABLED
    return machine->sound().musicTicks();
#else
    return 0;
#endif
  }

  static int music_pattern()
  {
#if SOUND_ENABLED
    return machine->sound().musicPattern();
#else
    return -1;
#endif
  }

  int stat(lua_State* L)
  {
    const int n = lua_isnumber(L, 1) ? (int)lua_tonumber(L, 1) : 0;
    const int fps = machine->code().require60fps() ? 60 : 30;

    if (n >= 80 && n <= 95 && (n <= 85 || n >= 90))
    {
      const std::time_t now = std::time(nullptr);
      std::tm parts = {};
      if (n <= 85)
        gmtime_r(&now, &parts);
      else
        localtime_r(&now, &parts);
      const int values[] = { parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday, parts.tm_hour, parts.tm_min, parts.tm_sec };
      lua_pushinteger(L, values[(n - 80) % 10]);
      return 1;
    }

    switch (n)
    {
    case 0: lua_pushnumber(L, lua_gc(L, LUA_GCCOUNT, 0)); break;
    case 1: case 2: lua_pushnumber(L, machine->code().cpuLoad()); break;
    case 4: lua_pushstring(L, ""); break;
    case 31:
    {
      auto& typed = machine->state().keyboard.typed;
      if ((machine->memory().base()[0x5f2d] & 1) && !typed.empty())
      {
        lua_pushlstring(L, typed.data(), 1);
        typed.erase(0, 1);
      }
      else
        lua_pushstring(L, "");
      break;
    }
    case 6: lua_pushstring(L, lua::cartParam.c_str()); break;
    case 5: lua_pushinteger(L, 41); break;
    case 7: case 8: case 9: lua_pushinteger(L, fps); break;
    case 16: case 17: case 18: case 19: lua_pushinteger(L, channel_sound(n - 16)); break;
    case 46: case 47: case 48: case 49: lua_pushinteger(L, channel_sound(n - 46)); break;
    case 20: case 21: case 22: case 23: lua_pushinteger(L, channel_note(n - 20)); break;
    case 50: case 51: case 52: case 53: lua_pushinteger(L, channel_note(n - 50)); break;
    case 24: case 54: lua_pushinteger(L, music_pattern()); break;
    case 26: case 56: lua_pushinteger(L, music_ticks()); break;
    case 57: lua_pushboolean(L, music_pattern() >= 0); break;
    case 28:
    {
      const int code = lua_isnumber(L, 2) ? (int)lua_tonumber(L, 2) : -1;
      const auto& held = machine->state().keyboard.held;
      lua_pushboolean(L, (machine->memory().base()[0x5f2d] & 1) && code >= 0 && code < (int)held.size() && held[code]);
      break;
    }
    case 30: lua_pushboolean(L, (machine->memory().base()[0x5f2d] & 1) && !machine->state().keyboard.typed.empty()); break;
    case 110: case 120: case 121: lua_pushboolean(L, 0); break;
    case 32: case 33: case 34: case 36: case 38: case 39:
    {
      const auto& mouse = machine->state().mouse;
      int32_t value = 0;
      if (machine->memory().base()[0x5f2d] & 1)
        switch (n)
        {
        case 32: value = mouse.x; break;
        case 33: value = mouse.y; break;
        case 34: value = mouse.buttons; break;
        case 36: value = mouse.wheel; break;
        case 38: value = mouse.dx; break;
        default: value = mouse.dy; break;
        }
      lua_pushinteger(L, value);
      break;
    }
    case 100: if (lua::cartBreadcrumb.empty()) lua_pushnil(L); else lua_pushstring(L, lua::cartBreadcrumb.c_str()); break;
    case 101: lua_pushnil(L); break;
    default: lua_pushinteger(L, 0); break;
    }

    return 1;
  }

  static constexpr int32_t CART_DATA_ADDRESS = 0x5e00;
  static constexpr size_t CART_DATA_SIZE = 256;

  static std::string cartdataPath;
  static uint8_t cartdataSaved[CART_DATA_SIZE];

  /* the save is PICO-8's .p8d.txt layout, 64 values of 8 hex digits, 8 to a line */
  int cartdata(lua_State* L)
  {
    if (!cartdataPath.empty() || !lua_isstring(L, 1) || cartdataDirectory.empty())
    {
      lua_pushboolean(L, 0);
      return 1;
    }

    std::string id = lua_tostring(L, 1);
    for (char& c : id)
      if (!std::isalnum((unsigned char)c) && c != '_')
        c = '_';

    cartdataPath = cartdataDirectory + "/" + id + ".p8d.txt";
    uint8_t* data = mem() + CART_DATA_ADDRESS;
    bool loaded = false;

    if (FILE* file = std::fopen(cartdataPath.c_str(), "r"))
    {
      uint32_t value = 0;
      int digits = 0, index = 0, ch;
      while ((ch = std::fgetc(file)) != EOF && index < 64)
      {
        if (!std::isxdigit(ch))
          continue;
        value = (value << 4) | (uint32_t)(std::isdigit(ch) ? ch - '0' : (std::tolower(ch) - 'a' + 10));
        if (++digits == 8)
        {
          for (int b = 0; b < 4; ++b)
            data[index * 4 + b] = (value >> (b * 8)) & 0xff;
          ++index;
          value = 0;
          digits = 0;
        }
      }
      std::fclose(file);
      loaded = index > 0;
    }

    std::memcpy(cartdataSaved, data, CART_DATA_SIZE);
    lua_pushboolean(L, loaded);
    return 1;
  }

  int dset(lua_State* L)
  {
    const int idx = lua_isnumber(L, 1) ? (int)lua_tonumber(L, 1) : -1;
    if (idx < 0 || idx > 63)
      return 0;

    const uint32_t value = (uint32_t)fixed_at(L, 2);
    for (int b = 0; b < 4; ++b)
      write8(CART_DATA_ADDRESS + idx * 4 + b, (value >> (b * 8)) & 0xff);
    return 0;
  }

  int dget(lua_State* L)
  {
    const int idx = lua_isnumber(L, 1) ? (int)lua_tonumber(L, 1) : -1;
    if (idx < 0 || idx > 63)
    {
      lua_pushinteger(L, 0);
      return 1;
    }

    uint32_t value = 0;
    for (int b = 0; b < 4; ++b)
      value |= (uint32_t)read8(CART_DATA_ADDRESS + idx * 4 + b) << (b * 8);
    const int32_t fixed = (int32_t)value;
    if ((fixed & 0xffff) == 0)
      lua_pushinteger(L, fixed >> 16);
    else
      lua_pushnumber(L, fixed / 65536.0);
    return 1;
  }

  /* load(cart, [breadcrumb], [param]) ends this cart and starts another from the same folder once the frame is over; anything
   * that is not a cart file goes to Lua's own load, which PICO-8 does not have */
  int load(lua_State* L)
  {
    const std::string path = lua_isstring(L, 1) && !lua_isnumber(L, 1) ? retro8::io::findCart(lua_tostring(L, 1)) : std::string();

    if (path.empty())
    {
      lua_getfield(L, LUA_REGISTRYINDEX, "retro8.lua_load");
      if (!lua_isfunction(L, -1))
      {
        lua_pushboolean(L, 0);
        return 1;
      }
      lua_insert(L, 1);
      lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
      return lua_gettop(L);
    }

    lua::pendingCart = path;
    lua::pendingParam = lua_isstring(L, 3) ? lua_tostring(L, 3) : "";
    lua::cartBreadcrumb = lua_isstring(L, 2) ? lua_tostring(L, 2) : "";

    if (machine->code().isCartThread(L) && lua_isyieldable(L))
      return lua_yield(L, 0);

    lua_pushboolean(L, 1);
    return 1;
  }

  /* flip() ends the frame early, and the cart carries on from here on the next one */
  int flip(lua_State* L)
  {
    if (machine->code().isCartThread(L) && lua_isyieldable(L))
      return lua_yield(L, 0);

    return 0;
  }

  int extcmd(lua_State* L)
  {
    //TODO: implement
    return 0;
  }

  int menuitem(lua_State* L)
  {
    //TODO: implement
    return 0;
  }

  int time(lua_State* L)
  {
    /* PICO-8 time moves on by one frame every frame, whatever the real clock does */
    lua_pushnumber(L, machine->code().time());
    return 1;
  }

  int printh(lua_State* L)
  {
    //TODO: finish implementing additional parameters
    
    std::string text = lua_tostring(L, 1);
    std::cout << text << std::endl;

    return 0;
  }
}

#pragma warning(pop)

void lua::registerFunctions(lua_State* L)
{
  lua_register(L, "pset", pset);
  lua_register(L, "pget", pget);
  lua_register(L, "pal", pal);
  lua_register(L, "palt", palt);
  lua_register(L, "color", color);
  lua_register(L, "line", line);
  lua_register(L, "fillp", fillp);
  lua_register(L, "rect", rect);
  lua_register(L, "rectfill", rectfill);
  lua_register(L, "circ", circ);
  lua_register(L, "circfill", circfill);
  lua_register(L, "clip", draw::clip);
  lua_register(L, "cls", cls);
  lua_register(L, "spr", spr);
  lua_register(L, "camera", camera);
  lua_register(L, "map", map);
  lua_register(L, "tline", tline);
  lua_register(L, "mget", mget);
  lua_register(L, "mset", mset);
  lua_register(L, "sget", sget);
  lua_register(L, "sset", sset);

  lua_register(L, "print", print);
  lua_register(L, "cursor", cursor);

  lua_register(L, "fset", sprites::fset);
  lua_register(L, "fget", sprites::fget);
  lua_register(L, "sspr", sprites::sspr);

  lua_register(L, "__debugprint", debug::debugprint);
  lua_register(L, "__breakpoint", debug::breakpoint);

  lua_register(L, "cos", math::cos);
  lua_register(L, "sin", math::sin);
  lua_register(L, "atan2", math::atan2);
  lua_register(L, "srand", math::srand);
  lua_register(L, "rnd", math::rnd);
  lua_register(L, "flr", math::flr);
  lua_register(L, "ceil", math::ceil);
  lua_register(L, "min", math::min);
  lua_register(L, "max", math::max);
  lua_register(L, "mid", math::mid);
  lua_register(L, "abs", math::abs);
  lua_register(L, "sgn", math::sgn);
  lua_register(L, "sqrt", math::sqrt);

  lua_register(L, "band", bitwise::band);
  lua_register(L, "bor", bitwise::bor);
  lua_register(L, "bxor", bitwise::bxor);
  lua_register(L, "bnot", bitwise::bnot);
  lua_register(L, "shl", bitwise::shl);
  lua_register(L, "shr", bitwise::shr);
  lua_register(L, "lshl", bitwise::lshl);
  lua_register(L, "lshr", bitwise::lshr);
  lua_register(L, "rotl", bitwise::rotl);
  lua_register(L, "rotr", bitwise::rotr);

  lua_register(L, "music", ::sound::music);
  lua_register(L, "sfx", ::sound::sfx);

  lua_register(L, "sub", string::sub);
  lua_register(L, "tostr", string::tostr);
  lua_register(L, "tonum", string::tonum);

  lua_register(L, "btn", platform::btn);
  lua_register(L, "btnp", platform::btnp);
  lua_register(L, "time", platform::time);
  lua_register(L, "t", platform::time);
  lua_register(L, "extcmd", platform::extcmd);
  lua_register(L, "menuitem", platform::menuitem);
  lua_register(L, "stat", platform::stat);
  lua_register(L, "cartdata", platform::cartdata);
  lua_register(L, "dset", platform::dset);
  lua_register(L, "dget", platform::dget);
  lua_register(L, "poke", platform::poke);
  lua_register(L, "peek", platform::peek);
  lua_register(L, "poke2", platform::poke2);
  lua_register(L, "peek2", platform::peek2);
  lua_register(L, "poke4", platform::poke4);
  lua_register(L, "peek4", platform::peek4);
  lua_register(L, "memset", platform::memset);
  lua_register(L, "memcpy", platform::memcpy);
  lua_register(L, "reload", platform::reload);

  lua_getfield(L, LUA_REGISTRYINDEX, "retro8.lua_load");
  const bool saved = lua_isfunction(L, -1);
  lua_pop(L, 1);
  if (!saved)
  {
    lua_getglobal(L, "load");
    lua_setfield(L, LUA_REGISTRYINDEX, "retro8.lua_load");
  }
  lua_register(L, "load", platform::load);
  lua_register(L, "printh", platform::printh);

  lua_register(L, "flip", platform::flip);
}

Code::~Code()
{
  if (L)
    lua_close(L);
}

/* Lua frees a block with its size, so blocks up to 256 bytes come from free lists per 16 byte size class, carved from
   64 KB chunks, instead of malloc and free on every table, closure and string the cart makes */
namespace pool
{
  constexpr size_t STEP = 16, LIMIT = 256, CHUNK = 64 * 1024;

  struct Block { Block* next; };
  Block* freed[LIMIT / STEP + 1];
  uint8_t* at = nullptr;
  size_t left = 0;

  inline size_t sizeClass(size_t size) { return size ? (size + STEP - 1) / STEP : 1; }

  void* take(size_t cls)
  {
    if (Block* block = freed[cls])
    {
      freed[cls] = block->next;
      return block;
    }

    const size_t bytes = cls * STEP;
    if (left < bytes)
    {
      at = static_cast<uint8_t*>(malloc(CHUNK));
      left = at ? CHUNK : 0;
      if (!at)
        return nullptr;
    }

    void* block = at;
    at += bytes;
    left -= bytes;
    return block;
  }

  void give(void* ptr, size_t cls)
  {
    Block* block = static_cast<Block*>(ptr);
    block->next = freed[cls];
    freed[cls] = block;
  }

  void* alloc(void*, void* ptr, size_t osize, size_t nsize)
  {
    const bool oldSmall = ptr && osize <= LIMIT;
    const bool newSmall = nsize <= LIMIT;

    if (nsize == 0)
    {
      if (oldSmall)
        give(ptr, sizeClass(osize));
      else
        free(ptr);
      return nullptr;
    }

    if (!ptr)
      return newSmall ? take(sizeClass(nsize)) : malloc(nsize);
    if (oldSmall && newSmall && sizeClass(osize) == sizeClass(nsize))
      return ptr;
    if (!oldSmall && !newSmall)
      return realloc(ptr, nsize);

    void* moved = newSmall ? take(sizeClass(nsize)) : malloc(nsize);
    if (!moved)
      return nullptr;
    memcpy(moved, ptr, std::min(osize, nsize));
    if (oldSmall)
      give(ptr, sizeClass(osize));
    else
      free(ptr);
    return moved;
  }

  int panic(lua_State* L)
  {
    fprintf(stderr, "PANIC: unprotected error in call to Lua API (%s)\n", lua_tostring(L, -1));
    return 0;
  }

  lua_State* newState()
  {
    lua_State* L = lua_newstate(alloc, nullptr);
    if (L)
      lua_atpanic(L, panic);
    return L;
  }
}

void Code::loadAPI()
{
  platform::cartdataPath.clear();

  if (!L)
  {
    L = pool::newState();
  }

  luaL_openlibs(L);

  /*std::ifstream apiFile("api.lua");
  std::string api((std::istreambuf_iterator<char>(apiFile)), std::istreambuf_iterator<char>());*/

  LOGD("Loading extended PICO-8 Api");

  /* the API is written with string and math, which carts themselves do not get */
  if (luaL_loadstring(L, lua_api_string) != LUA_OK)
    printError("api.lua loading");
  else
  {
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 0);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 0);
    if (lua_pcall(L, 2, 0, 0) != LUA_OK)
      printError("api.lua loading");
  }

  lua_pushliteral(L, "");
  lua_newtable(L);
  lua_pushcfunction(L, string_index);
  lua_setfield(L, -2, "__index");
  lua_setmetatable(L, -2);
  lua_pop(L, 1);

  lua_register(L, "_set_fps", set_fps);
}

void Code::printError(const char* where)
{
  //for (int i = 1; i < lua_gettop(L); ++i)
  {
    std::cout << "Error on " << where << std::endl;

    //luaL_traceback(L, L, NULL, 1);
    //printf("%s\n", lua_tostring(L, -1));

    if (lua_isstring(L, -1))
    {
      const char* message = lua_tostring(L, -1);
      std::cout << message << std::endl;
    }
  }
  lua_settop(L, 0);
}

bool Code::initFromSource(const std::string& code)
{
  if (!L)
    L = pool::newState();

  registerFunctions(L);

  /* the API as it stands before the cart runs, for carts that replace _ENV */
  lua_newtable(L);
  lua_pushglobaltable(L);
  lua_pushnil(L);
  while (lua_next(L, -2))
  {
    if (lua_type(L, -2) == LUA_TSTRING && lua_isfunction(L, -1))
    {
      lua_pushvalue(L, -2);
      lua_insert(L, -2);
      lua_rawset(L, -5);
    }
    else
      lua_pop(L, 1);
  }
  lua_pop(L, 1);
  lua_setfield(L, LUA_REGISTRYINDEX, "retro8.api");

  buildPermanents();

  if (luaL_loadstring(L, code.c_str()) != LUA_OK)
  {
    printError("luaL_loadString");
    return false;
  }

  start(PHASE_TOP);
  return true;
}

namespace
{
  /* records a value as a permanent under a name, keeping the first name a value is found under */
  void addPermanent(lua_State* L, int perm, int unperm, int value, const std::string& name)
  {
    value = lua_absindex(L, value);
    const int type = lua_type(L, value);
    if (type != LUA_TFUNCTION && type != LUA_TTABLE && type != LUA_TUSERDATA && type != LUA_TTHREAD && type != LUA_TLIGHTUSERDATA)
      return;

    lua_pushvalue(L, value);
    lua_rawget(L, perm);
    const bool known = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (known)
      return;

    lua_pushvalue(L, value);
    lua_pushstring(L, name.c_str());
    lua_rawset(L, perm);

    lua_pushstring(L, name.c_str());
    lua_pushvalue(L, value);
    lua_rawset(L, unperm);
  }

  /* a table and the values one level inside it */
  void addPermanentTable(lua_State* L, int perm, int unperm, int table, const std::string& name)
  {
    table = lua_absindex(L, table);
    addPermanent(L, perm, unperm, table, name);
    if (lua_type(L, table) != LUA_TTABLE)
      return;

    lua_pushnil(L);
    while (lua_next(L, table))
    {
      if (lua_type(L, -2) == LUA_TSTRING)
        addPermanent(L, perm, unperm, -1, name + "." + lua_tostring(L, -2));
      lua_pop(L, 1);
    }
  }

  struct Blob
  {
    std::string data;

    template<typename T> void put(const T& value) { data.append(reinterpret_cast<const char*>(&value), sizeof(T)); }
    void putString(const std::string& value) { put<uint32_t>(uint32_t(value.size())); data.append(value); }
  };

  struct Reader
  {
    const char* at;
    const char* end;

    template<typename T> bool get(T& value)
    {
      if (size_t(end - at) < sizeof(T)) return false;
      std::memcpy(&value, at, sizeof(T));
      at += sizeof(T);
      return true;
    }

    bool getString(std::string& value)
    {
      uint32_t length = 0;
      if (!get(length) || size_t(end - at) < length) return false;
      value.assign(at, length);
      at += length;
      return true;
    }
  };

  int persistRoot(lua_State* L)
  {
    eris_persist(L, 1, 2);
    return 1;
  }

  int unpersistRoot(lua_State* L)
  {
    eris_unpersist(L, 1, 2);
    return 1;
  }
}

void Code::buildPermanents()
{
  lua_newtable(L);
  const int perm = lua_gettop(L);
  lua_newtable(L);
  const int unperm = lua_gettop(L);
  lua_newtable(L);
  const int original = lua_gettop(L);

  lua_pushglobaltable(L);
  addPermanent(L, perm, unperm, -1, "g");
  lua_pushnil(L);
  while (lua_next(L, -2))
  {
    if (lua_type(L, -2) == LUA_TSTRING)
    {
      addPermanentTable(L, perm, unperm, -1, std::string("g.") + lua_tostring(L, -2));
      lua_pushvalue(L, -2);
      lua_pushvalue(L, -2);
      lua_rawset(L, original);
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);

  lua_getfield(L, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
  lua_pushnil(L);
  while (lua_next(L, -2))
  {
    if (lua_type(L, -2) == LUA_TSTRING)
      addPermanentTable(L, perm, unperm, -1, std::string("lib.") + lua_tostring(L, -2));
    lua_pop(L, 1);
  }
  lua_pop(L, 1);

  const char* registryNames[] = { "retro8.api", "retro8.lua_load" };
  for (const char* name : registryNames)
  {
    lua_getfield(L, LUA_REGISTRYINDEX, name);
    addPermanentTable(L, perm, unperm, -1, std::string("reg.") + name);
    lua_pop(L, 1);
  }

  lua_pushliteral(L, "");
  if (lua_getmetatable(L, -1))
  {
    addPermanentTable(L, perm, unperm, -1, "strmt");
    lua_pop(L, 1);
  }
  lua_pop(L, 1);

  lua_pushthread(L);
  addPermanent(L, perm, unperm, -1, "mainthread");
  lua_pop(L, 1);

  /* a print waiting for frames resumes through this continuation, which Eris stores by name like any C function */
  lua_pushcfunction(L, (lua_CFunction)(void (*)(void))print_continue);
  addPermanent(L, perm, unperm, -1, "k.print_continue");
  lua_pop(L, 1);

  lua_setfield(L, LUA_REGISTRYINDEX, "retro8.globals");
  lua_setfield(L, LUA_REGISTRYINDEX, "retro8.unperm");
  lua_setfield(L, LUA_REGISTRYINDEX, "retro8.perm");
}

bool Code::saveState(std::string& out)
{
  if (!L)
    return false;

  const int top = lua_gettop(L);

  lua_pushcfunction(L, persistRoot);
  lua_getfield(L, LUA_REGISTRYINDEX, "retro8.perm");
  if (!lua_istable(L, -1))
  {
    lua_settop(L, top);
    return false;
  }

  lua_newtable(L);
  const int root = lua_gettop(L);
  lua_getfield(L, LUA_REGISTRYINDEX, "retro8.globals");
  const int original = lua_gettop(L);

  /* only the globals the cart has added or changed are stored, the rest are already here when it loads */
  lua_newtable(L);
  lua_pushglobaltable(L);
  lua_pushnil(L);
  while (lua_next(L, -2))
  {
    lua_pushvalue(L, -2);
    lua_rawget(L, original);
    const bool same = lua_rawequal(L, -1, -2);
    lua_pop(L, 1);
    if (same)
      lua_pop(L, 1);
    else
    {
      lua_pushvalue(L, -2);
      lua_insert(L, -2);
      lua_rawset(L, -5);
    }
  }
  lua_pop(L, 1);
  lua_setfield(L, root, "g");

  /* and API globals the cart has cleared, so they stay cleared */
  lua_newtable(L);
  const int removedList = lua_gettop(L);
  int removed = 0;
  lua_pushnil(L);
  while (lua_next(L, original))
  {
    lua_pop(L, 1);
    lua_pushglobaltable(L);
    lua_pushvalue(L, -2);
    lua_rawget(L, -2);
    const bool gone = lua_isnil(L, -1);
    lua_pop(L, 2);
    if (gone)
    {
      lua_pushvalue(L, -1);
      lua_rawseti(L, removedList, ++removed);
    }
  }
  lua_setfield(L, root, "removed");
  lua_pop(L, 1);

  if (_suspended && _thread)
  {
    lua_pushthread(_thread);
    lua_xmove(_thread, L, 1);
    lua_setfield(L, root, "thread");
  }

  /* the collector stays off while Eris works, as it walks objects Eris has only half built */
  lua_gc(L, LUA_GCSTOP, 0);
  const int persistStatus = lua_pcall(L, 2, 1, 0);
  lua_gc(L, LUA_GCRESTART, 0);
  if (persistStatus != LUA_OK)
  {
    std::cout << "Save state failed: " << (lua_isstring(L, -1) ? lua_tostring(L, -1) : "unknown error") << std::endl;
    lua_settop(L, top);
    return false;
  }

  size_t length = 0;
  const char* persisted = lua_tolstring(L, -1, &length);

  Blob blob;
  blob.put<double>(_time);
  blob.put<int32_t>(int32_t(_phase));
  blob.put<int32_t>(_suspended ? 1 : 0);
  blob.put<int32_t>(_waitFrames);
  blob.put<int32_t>(_fps);
  blob.putString(std::string(persisted, length));
  out = std::move(blob.data);

  lua_settop(L, top);
  return true;
}

bool Code::loadState(const std::string& in)
{
  if (!L)
    return false;

  Reader reader = { in.data(), in.data() + in.size() };
  double time = 0.0;
  int32_t phase = 0, suspended = 0, waitFrames = 0, fps = 0;
  std::string persisted;
  if (!reader.get(time) || !reader.get(phase) || !reader.get(suspended) || !reader.get(waitFrames) || !reader.get(fps) ||
      !reader.getString(persisted))
    return false;

  const int top = lua_gettop(L);

  lua_pushcfunction(L, unpersistRoot);
  lua_getfield(L, LUA_REGISTRYINDEX, "retro8.unperm");
  if (!lua_istable(L, -1))
  {
    lua_settop(L, top);
    return false;
  }
  lua_pushlstring(L, persisted.data(), persisted.size());

  lua_gc(L, LUA_GCSTOP, 0);
  const int unpersistStatus = lua_pcall(L, 2, 1, 0);
  lua_gc(L, LUA_GCRESTART, 0);
  if (unpersistStatus != LUA_OK || !lua_istable(L, -1))
  {
    std::cout << "Load state failed: " << (lua_isstring(L, -1) ? lua_tostring(L, -1) : "unknown error") << std::endl;
    lua_settop(L, top);
    return false;
  }
  const int root = lua_gettop(L);

  lua_getfield(L, LUA_REGISTRYINDEX, "retro8.globals");
  const int original = lua_gettop(L);
  lua_pushglobaltable(L);
  const int globals = lua_gettop(L);

  /* the globals go back to exactly what they were: anything the cart added since is cleared first */
  lua_newtable(L);
  const int clear = lua_gettop(L);
  int clearCount = 0;
  lua_pushnil(L);
  while (lua_next(L, globals))
  {
    lua_pop(L, 1);
    lua_pushvalue(L, -1);
    lua_rawget(L, original);
    const bool keep = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (!keep)
    {
      lua_pushvalue(L, -1);
      lua_rawseti(L, clear, ++clearCount);
    }
  }
  for (int i = 1; i <= clearCount; ++i)
  {
    lua_rawgeti(L, clear, i);
    lua_pushnil(L);
    lua_rawset(L, globals);
  }

  lua_pushnil(L);
  while (lua_next(L, original))
  {
    lua_pushvalue(L, -2);
    lua_insert(L, -2);
    lua_rawset(L, globals);
  }

  lua_getfield(L, root, "g");
  lua_pushnil(L);
  while (lua_next(L, -2))
  {
    lua_pushvalue(L, -2);
    lua_insert(L, -2);
    lua_rawset(L, globals);
  }
  lua_pop(L, 1);

  lua_getfield(L, root, "removed");
  const lua_Integer removedCount = luaL_len(L, -1);
  for (lua_Integer i = 1; i <= removedCount; ++i)
  {
    lua_rawgeti(L, -1, i);
    lua_pushnil(L);
    lua_rawset(L, globals);
  }
  lua_pop(L, 1);

  dropThread();
  lua_getfield(L, root, "thread");
  if (lua_isthread(L, -1))
  {
    _thread = lua_tothread(L, -1);
    _threadRef = luaL_ref(L, LUA_REGISTRYINDEX);
  }
  else
    lua_pop(L, 1);

  _time = time;
  _phase = Phase(phase);
  _suspended = suspended && _thread;
  _waitFrames = waitFrames;
  _fps = fps;

  lua_settop(L, top);
  lookupCallbacks();
  return true;
}

void Code::lookupCallbacks()
{
  const void** slots[] = { &_update, &_update60, &_draw, &_init };
  const char* names[] = { "_update", "_update60", "_draw", "_init" };

  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
  {
    lua_getglobal(L, names[i]);
    *slots[i] = lua_isfunction(L, -1) ? lua_topointer(L, -1) : nullptr;
    lua_pop(L, 1);
  }
}

/* cart code runs on its own thread so that flip() can hand a frame back and carry on later */
void Code::start(Phase phase)
{
  if (!_thread)
  {
    _thread = lua_newthread(L);
    _threadRef = luaL_ref(L, LUA_REGISTRYINDEX);
  }

  lua_settop(_thread, 0);
  lua_xmove(L, _thread, 1);
  _phase = phase;
  step();
}

void Code::step()
{
  static const char* names[] = { "lua_pcall on init", "_init", "_update", "_draw" };

  const int status = lua_resume(_thread, L, 0);
  _suspended = status == LUA_YIELD;
  if (_suspended)
    return;

  if (status != LUA_OK)
  {
    luaL_traceback(L, _thread, lua_tostring(_thread, -1), 0);
    printError(names[_phase]);
    dropThread();
  }
  else
    lua_settop(_thread, 0);

  if (_phase == PHASE_TOP)
    lookupCallbacks();
}

void Code::dropThread()
{
  if (_thread)
    luaL_unref(L, LUA_REGISTRYINDEX, _threadRef);
  _thread = nullptr;
  _suspended = false;
}

static double seconds_now()
{
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

/* stat(1) is how much of this frame's time the cart has used so far, which carts use to scale their detail */
double Code::cpuLoad() const
{
  if (_frameStart <= 0.0)
    return 0.0;
  return (seconds_now() - _frameStart) * (require60fps() ? 60.0 : 30.0);
}

void Code::frame()
{
  _frameStart = seconds_now();
  _time += require60fps() ? 1.0 / 60.0 : 1.0 / 30.0;

  if (_waitFrames > 0)
  {
    --_waitFrames;
    return;
  }

  /* carts may set or swap _update and _draw at any time, so they are looked up again for every frame */
  if (!_suspended)
    lookupCallbacks();

  if (_suspended)
  {
    const Phase phase = _phase;
    step();
    if (_suspended)
      return;

    if (phase == PHASE_TOP)
      init();
    else if (phase == PHASE_UPDATE)
      draw();
    return;
  }

  update();
  if (!_suspended)
    draw();
}

void Code::callFunction(const char* name, int ret)
{
  if (_suspended)
    return;

  lua_getglobal(L, name);
  if (!lua_isfunction(L, -1))
  {
    lua_pop(L, 1);
    return;
  }

  Phase phase = PHASE_UPDATE;
  if (std::strcmp(name, "_init") == 0)
    phase = PHASE_INIT;
  else if (std::strcmp(name, "_draw") == 0)
    phase = PHASE_DRAW;

  start(phase);
}

void Code::update()
{
  if (_update60)
    callFunction("_update60");
  else if (_update)
    callFunction("_update");
}

void Code::draw()
{
  if (_draw)
    callFunction("_draw");
}

void Code::init()
{
  if (_init)
    callFunction("_init");
}

std::string lua::cartdataDirectory;
std::string lua::pendingCart;
std::string lua::pendingParam;
std::string lua::cartParam;
std::string lua::cartBreadcrumb;

void lua::cartdataFlush()
{
  using namespace platform;
  if (cartdataPath.empty())
    return;

  const uint8_t* data = mem() + CART_DATA_ADDRESS;
  if (std::memcmp(cartdataSaved, data, CART_DATA_SIZE) == 0)
    return;

  if (FILE* file = std::fopen(cartdataPath.c_str(), "w"))
  {
    for (int i = 0; i < 64; ++i)
    {
      const uint32_t value = data[i * 4] | (data[i * 4 + 1] << 8) | (data[i * 4 + 2] << 16) | ((uint32_t)data[i * 4 + 3] << 24);
      std::fprintf(file, "%08x%s", value, i % 8 == 7 ? "\n" : "");
    }
    std::fclose(file);
    std::memcpy(cartdataSaved, data, CART_DATA_SIZE);
  }
}
