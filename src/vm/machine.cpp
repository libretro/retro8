#include <cstring>
#include <vector>
#include "machine.h"

#include <algorithm>
#include <sstream>

using namespace retro8;

void Machine::color(color_t color)
{
  gfx::color_byte_t* penColor = _memory.penColor();
  penColor->low(color);
}

void Machine::cls(color_t color)
{
  color = _memory.paletteAt(gfx::DRAW_PALETTE_INDEX)->get(color);
  gfx::color_byte_t value = gfx::color_byte_t(color, color);

  auto* data = _memory.screenData();
  memset(data, value.value, gfx::BYTES_PER_SCREEN);

  _memory.clipRect()->reset();
  *_memory.cursor() = { 0, 0 };
}

void Machine::pset(coord_t x, coord_t y, color_t color)
{
  auto* clip = _memory.clipRect();
  x -= memory().camera()->x();
  y -= memory().camera()->y();

  if (x >= clip->x0 && x < clip->x1 && y >= clip->y0 && y < clip->y1)
  {
    color = _memory.paletteAt(gfx::DRAW_PALETTE_INDEX)->get(color_t(color % gfx::COLOR_COUNT));
    plot(_memory.screenData(0, y), x, color, bitplanes());
  }
}

/* fillp: pattern bit 15 is the top left pixel of each 4x4 cell; a set bit draws the high nibble, or nothing when transparent */
int Machine::patterned(coord_t x, coord_t y, color_t color)
{
  const uint8_t* fill = _memory.base() + address::FILL_PATTERN;
  const uint16_t pattern = fill[0] | (fill[1] << 8);

  if (pattern & (0x8000 >> (((y & 3) << 2) | (x & 3))))
    return (fill[2] & 1) ? -1 : (color >> 4) & 0x0f;
  return color & 0x0f;
}

void Machine::fill(coord_t x, coord_t y, color_t color)
{
  const int c = patterned(x - memory().camera()->x(), y - memory().camera()->y(), color);

  if (c >= 0)
    pset(x, y, color_t(c));
}

color_t Machine::pget(coord_t x, coord_t y)
{
  x -= memory().camera()->x();
  y -= memory().camera()->y();

  if (x < 0 || y < 0 || x >= (coord_t)gfx::SCREEN_WIDTH || y >= (coord_t)gfx::SCREEN_HEIGHT)
    return static_cast<color_t>(0);
  return _memory.screenData(x, y)->get(x);
}

void Machine::line(coord_t x0, coord_t y0, coord_t x1, coord_t y1, color_t color)
{
  // vertical
  if (y0 == y1)
  {
    if (x0 > x1) std::swap(x0, x1);

    for (coord_t x = x0; x <= x1; ++x)
      fill(x, y0, color);
  }
  // horizontal
  else if (x0 == x1)
  {
    if (y0 > y1) std::swap(y0, y1);

    for (coord_t y = y0; y <= y1; ++y)
      fill(x0, y, color);
  }
  else
  {
    coord_t dx = abs(x1 - x0);
    coord_t sx = x0 < x1 ? 1 : -1;
    coord_t dy = -abs(y1 - y0);
    coord_t sy = y0 < y1 ? 1 : -1;
    coord_t err = dx + dy;

    while (true)
    {
      fill(x0, y0, color);

      if (x0 == x1 && y0 == y1)
        break;

      coord_t err2 = 2 * err;

      if (err2 >= dy)
      {
        err += dy;
        x0 += sx;
      }

      if (err2 <= dx)
      {
        err += dx;
        y0 += sy;
      }
    }
  }

  // TODO: shouldn't be updated when invoked from other primitives, eg rect
  _state.lastLineEnd.x = x1;
  _state.lastLineEnd.y = y1;
}

void Machine::rect(coord_t x0, coord_t y0, coord_t x1, coord_t y1, color_t color)
{
  line(x0, y0, x1, y0, color);
  line(x1, y0, x1, y1, color);
  line(x0, y1, x1, y1, color);
  line(x0, y1, x0, y0, color);
}

void Machine::rectfill(coord_t x0, coord_t y0, coord_t x1, coord_t y1, color_t color)
{
#if R8_OPTS_ENABLED

  /* compute directly actual bounding box and set the rect without invoking pset */

  auto* clip = _memory.clipRect();
  auto cx = memory().camera()->x(), cy = memory().camera()->y();

  if (x0 > x1) std::swap(x0, x1);
  if (y0 > y1) std::swap(y0, y1);

  x0 -= cx;
  y0 -= cy;
  x1 -= cx;
  y1 -= cy;

  x0 = std::max(x0, coord_t(clip->x0));
  x1 = std::min(x1, coord_t(clip->x1 - 1));
  y0 = std::max(y0, coord_t(clip->y0));
  y1 = std::min(y1, coord_t(clip->y1 - 1));

  auto* palette = _memory.paletteAt(gfx::DRAW_PALETTE_INDEX);
  const uint8_t* fill = _memory.base() + address::FILL_PATTERN;

  const uint8_t mask = bitplanes();

  if (!fill[0] && !fill[1] && !mask)
  {
    /* an odd first or even last pixel shares its byte, everything between is whole bytes */
    const color_t solid = palette->get(color_t(color & 0x0f));
    const uint8_t pair = uint8_t(solid | (solid << 4));

    for (coord_t y = y0; y <= y1; ++y)
    {
      gfx::color_byte_t* row = _memory.screenData(0, y);
      coord_t x = x0;
      if (x <= x1 && (x & 1))
        row[x >> 1].high(solid), ++x;
      if (x1 >= x + 1)
      {
        const coord_t bytes = (x1 + 1 - x) >> 1;
        memset(row + (x >> 1), pair, bytes);
        x += bytes * 2;
      }
      if (x <= x1)
        row[x >> 1].low(solid);
    }
    return;
  }

  for (coord_t y = y0; y <= y1; ++y)
    for (coord_t x = x0; x <= x1; ++x)
    {
      const int c = patterned(x, y, color);
      if (c >= 0)
        plot(_memory.screenData(0, y), x, palette->get(color_t(c)), mask);
    }
#else
  for (coord_t y = y0; y <= y1; ++y)
    for (coord_t x = x0; x <= x1; ++x)
      fill(x, y, color);
#endif
}

void Machine::circHelper(coord_t xc, coord_t yc, coord_t x, coord_t y, color_t color)
{
  fill(xc + x, yc + y, color);
  fill(xc - x, yc + y, color);
  fill(xc + x, yc - y, color);
  fill(xc - x, yc - y, color);
  fill(xc + y, yc + x, color);
  fill(xc - y, yc + x, color);
  fill(xc + y, yc - x, color);
  fill(xc - y, yc - x, color);
}

void Machine::circ(coord_t xc, coord_t yc, amount_t r, color_t color)
{
  if (r < 0)
    return;

  //TODO: not identical to pico-8 but acceptable for now
  coord_t x = 0, y = r;
  float d = 3 - 2 * r;
  circHelper(xc, yc, x, y, color);

  while (y >= x)
  {
    x++;

    if (d > 0)
    {
      y--;
      d = d + 4 * (x - y) + 5;
    }
    else
      d = d + 4 * x + 3;

    circHelper(xc, yc, x, y, color);
  }
}

void Machine::circFillHelper(coord_t xc, coord_t yc, coord_t x, coord_t y, color_t col)
{
  //TODO: totally inefficient
  rectfill(xc - x, yc - y, xc + x, yc + y, col);
  rectfill(xc - y, yc - x, xc + y, yc + x, col);
}


/* the union of the rectangles circFillHelper would fill, drawn as one span per row */
void Machine::circfill(coord_t xc, coord_t yc, amount_t r, color_t color, bool inverted)
{
  if (r < 0)
    return;
  if (r > 0x7fff)
    r = 0x7fff;

  std::vector<coord_t> half(r + 2, -1);
  auto widen = [&half](coord_t row, coord_t width) {
    if (row >= 0 && row < (coord_t)half.size() && width > half[row])
      half[row] = width;
  };

  coord_t x = 0, y = r;
  float d = 3 - 2 * r;
  widen(y, x);
  widen(x, y);

  while (y >= x)
  {
    x++;

    if (d > 0)
    {
      y--;
      d = d + 3 * (x - y) + 5;
    }
    else
      d = d + 3 * x + 3;

    widen(y, x);
    widen(x, y);
  }

  for (coord_t row = (coord_t)half.size() - 2; row >= 0; --row)
    if (half[row + 1] > half[row])
      half[row] = half[row + 1];

  /* inverted fills every row of the clip area except the circle's own span */
  if (inverted)
  {
    const auto* clip = _memory.clipRect();
    const coord_t cx = memory().camera()->x(), cy = memory().camera()->y();
    const coord_t left = clip->x0 + cx, right = clip->x1 - 1 + cx;

    for (coord_t y = clip->y0 + cy; y < clip->y1 + cy; ++y)
    {
      const coord_t row = std::abs(y - yc);
      if (row > r || half[row] < 0)
        rectfill(left, y, right, y, color);
      else
      {
        if (xc - half[row] > left)
          rectfill(left, y, xc - half[row] - 1, y, color);
        if (xc + half[row] < right)
          rectfill(xc + half[row] + 1, y, right, y, color);
      }
    }
    return;
  }

  for (coord_t row = 0; row <= r; ++row)
  {
    if (half[row] < 0)
      continue;
    rectfill(xc - half[row], yc + row, xc + half[row], yc + row, color);
    if (row)
      rectfill(xc - half[row], yc - row, xc + half[row], yc - row, color);
  }
}

/* the part of a w by h block at x, y that the camera and clip leave on screen, as offsets into the block */
bool Machine::visible(coord_t& x, coord_t& y, coord_t w, coord_t h, coord_t& x0, coord_t& y0, coord_t& x1, coord_t& y1)
{
  const auto* clip = _memory.clipRect();
  x -= _memory.camera()->x();
  y -= _memory.camera()->y();

  x0 = std::max<coord_t>(0, coord_t(clip->x0) - x);
  y0 = std::max<coord_t>(0, coord_t(clip->y0) - y);
  x1 = std::min<coord_t>(w, std::min<coord_t>(clip->x1, gfx::SCREEN_WIDTH) - x);
  y1 = std::min<coord_t>(h, std::min<coord_t>(clip->y1, gfx::SCREEN_HEIGHT) - y);
  return x0 < x1 && y0 < y1;
}

void Machine::spr(index_t idx, coord_t x, coord_t y)
{
  coord_t x0, y0, x1, y1;
  if (!visible(x, y, gfx::SPRITE_WIDTH, gfx::SPRITE_HEIGHT, x0, y0, x1, y1))
    return;

  const gfx::sprite_t* sprite = _memory.spriteAt(idx);
  const gfx::palette_t* palette = _memory.paletteAt(gfx::DRAW_PALETTE_INDEX);
  const uint8_t mask = bitplanes();

  for (coord_t ty = y0; ty < y1; ++ty)
  {
    gfx::color_byte_t* row = _memory.screenData(0, y + ty);
    for (coord_t tx = x0; tx < x1; ++tx)
    {
      const color_t color = sprite->get(tx, ty);
      if (!palette->transparent(color))
        plot(row, x + tx, palette->get(color), mask);
    }
  }
}

void Machine::spr(index_t idx, coord_t bx, coord_t by, float sw, float sh, bool flipX, bool flipY)
{
  const gfx::palette_t* palette = _memory.paletteAt(gfx::DRAW_PALETTE_INDEX);

  coord_t w = sw * gfx::SPRITE_WIDTH;
  coord_t h = sh * gfx::SPRITE_HEIGHT;

  /* we bypass spriteAt since we can use directly the address */
  const gfx::color_byte_t* base = reinterpret_cast<const gfx::color_byte_t*>(_memory.spriteAt(idx));

  /* walk the destination that stays on screen and read back the source pixel each one shows */
  coord_t x0, y0, x1, y1;
  if (w <= 0 || h <= 0 || !visible(bx, by, w, h, x0, y0, x1, y1))
    return;
  const uint8_t mask = bitplanes();

  for (coord_t fy = y0; fy < y1; ++fy)
  {
    const coord_t y = flipY ? (h - fy - 1) : fy;
    const gfx::color_byte_t* source = base + y * gfx::SPRITE_SHEET_PITCH;
    gfx::color_byte_t* row = _memory.screenData(0, by + fy);

    for (coord_t fx = x0; fx < x1; ++fx)
    {
      const coord_t x = flipX ? (w - fx - 1) : fx;
      const color_t color = source[x >> 1].get(x & 1);

      if (!palette->transparent(color))
        plot(row, bx + fx, palette->get(color), mask);
    }
  }
}

void Machine::sspr(coord_t sx, coord_t sy, coord_t sw, coord_t sh, coord_t dx, coord_t dy, coord_t dw, coord_t dh, bool flipX, bool flipY)
{
  const gfx::palette_t* palette = _memory.paletteAt(gfx::DRAW_PALETTE_INDEX);

  if (dw <= 0 || dh <= 0)
    return;

  const float xr = sw / float(dw);
  const float yr = sh / float(dh);

  /* each target pixel samples the sheet from the far edge when flipped, and outside the sheet reads colour 0 */
  for (coord_t y = 0; y < dh; ++y)
  {
    const coord_t cy = sy + coord_t((flipY ? dh - 1 - y : y) * yr);

    for (coord_t x = 0; x < dw; ++x)
    {
      const coord_t cx = sx + coord_t((flipX ? dw - 1 - x : x) * xr);
      color_t color = color_t(0);

      if (cx >= 0 && cx < gfx::SPRITE_SHEET_WIDTH && cy >= 0 && cy < gfx::SPRITE_SHEET_HEIGHT)
        color = _memory.spriteSheet(cx, cy)->get(cx);

      if (!palette->transparent(color))
        pset(dx + x, dy + y, color);
    }
  }
}

// TODO: add support for strange characters like symbols
coord_t Machine::print(const std::string& string, coord_t x, coord_t y, color_t color, coord_t* bottom, PrintState* state)
{
  
  struct SpecialGlyph
  {
    std::vector<uint8_t> encoding;
    size_t index;
  };

  static const std::array<SpecialGlyph, 12> SpecialGlyphs = { {
    { { 0xe2, 0xac, 0x87, 0xef, 0xb8, 0x8f }, 3 }, // down arrow
    { { 0xe2, 0xac, 0x86, 0xef, 0xb8, 0x8f }, 20}, // up arrow
    { { 0xe2, 0xac, 0x85, 0xef, 0xb8, 0x8f }, 11}, // left arrow
    { { 0xe2, 0x9e, 0xa1, 0xef, 0xb8, 0x8f }, 17}, // right arrow
    { { 0xf0, 0x9f, 0x85, 0xbe, 0xef, 0xb8, 0x8f }, 14 }, // o button
    { { 0xe2, 0x9d, 0x8e }, 23 }, // x button

    // 0x8b left, 0x91 right, 0x94 up, 0x83 down, 0x83 o, 0x97 x
    { { 0x8b }, 11 }, { { 0x91, }, 17 }, { { 0x94 }, 20 }, { { 0x83 }, 3 }, { { 0x8e }, 14 }, { { 0x97 }, 23 } 
  } };

  /* P8SCII control code arguments are one character, 0-9 then a-z for 10-35 */
  auto argument = [&string](size_t at) -> int {
    if (at >= string.length()) return 0;
    const char a = string[at];
    if (a >= '0' && a <= '9') return a - '0';
    if (a >= 'a' && a <= 'z') return a - 'a' + 10;
    if (a >= 'A' && a <= 'Z') return a - 'A' + 10;
    return 0;
  };

  /* 0x5f58 sets default print attributes once bit 0 is on: 0x4 wide, 0x8 tall and 0x80 the custom font at 0x5600 */
  const uint8_t* ram = _memory.base();
  const uint8_t attributes = ram[0x5f58];
  const bool defaults = attributes & 0x01;

  coord_t sx = x;
  coord_t right = x;
  int wide = defaults && (attributes & 0x04) ? 2 : 1;
  int tall = defaults && (attributes & 0x08) ? 2 : 1;
  bool custom = defaults && (attributes & 0x80);
  auto fontHeight = [&]() -> coord_t { return custom ? ram[0x5602] : TEXT_LINE_HEIGHT; };
  coord_t lineHeight = fontHeight() * tall;
  size_t start = 0;

  if (state && state->position > 0)
  {
    x = state->x; y = state->y; sx = state->sx; right = state->right;
    wide = state->wide; tall = state->tall; lineHeight = state->lineHeight; color = state->color;
    custom = state->custom;
    start = state->position;
  }
  if (state)
    state->wait = 0;

  auto finish = [&]() {
    if (bottom)
      *bottom = y + lineHeight;
    return right;
  };

  for (size_t i = start; i < string.length(); ++i)
  {
    auto c = string[i];

    if (c == '\n')
    {
      y += lineHeight;
      lineHeight = fontHeight() * tall;
      x = sx;
      continue;
    }

    if ((uint8_t)c < 16)
    {
      switch (c)
      {
        case 0: return finish();                                /* end of string */
        case 1: i += 2; break;                                  /* \* repeat, not drawn */
        case 2: i += 1; break;                                  /* \# background colour */
        case 3: x += argument(i + 1) - 16; i += 1; break;       /* \- x shift */
        case 4: y += argument(i + 1) - 16; i += 1; break;       /* \| y shift */
        case 5: x += argument(i + 1) - 16; y += argument(i + 2) - 16; i += 2; break;  /* \+ */
        case 6:                                                 /* \^ special command */
        {
          const bool off = i + 1 < string.length() && string[i + 1] == '-';
          if (off) ++i;
          const char command = i + 1 < string.length() ? string[i + 1] : 0;
          i += 1;

          /* \^1 to \^9 wait 1, 2, 4 ... 256 frames before carrying on */
          if (command >= '1' && command <= '9' && state)
          {
            *state = { i + 1, 1 << (command - '1'), x, y, sx, right, lineHeight, wide, tall, color, custom };
            return finish();
          }
          if (command == 'c')
          {
            cls(static_cast<color_t>(argument(i + 1) & 0x0f));
            x = sx;
          }
          if (command == 'w' || command == 'p') wide = off ? 1 : 2;
          if (command == 't' || command == 'p') tall = off ? 1 : 2;
          if (tall * fontHeight() > lineHeight) lineHeight = tall * fontHeight();
          if (command && std::strchr("cdsxy", command)) i += 1;
          else if (command == 'j') i += 2;
          break;
        }
        case 7:                                                 /* \a audio, runs to a space */
          while (i + 1 < string.length() && string[i + 1] != ' ') ++i;
          break;
        case 8: x -= gfx::GLYPH_WIDTH; break;                   /* \b backspace */
        case 9: x = sx + ((x - sx) / 16 + 1) * 16; break;       /* \t tab */
        case 11: i += 2; break;                                 /* \v decoration */
        case 12: color = static_cast<color_t>(argument(i + 1) & 0x0f); i += 1; break;  /* \f colour */
        case 13: x = sx; break;                                 /* \r carriage return */
        case 14:                                                /* \14 custom font, \15 default font */
        case 15:
          custom = c == 14;
          if (fontHeight() * tall > lineHeight) lineHeight = fontHeight() * tall;
          break;
        default: break;                                         /* unused codes */
      }
      continue;
    }

    if (custom)
    {
      /* each character is 8 bytes from 0x5600, one row per byte with the lowest bit on the left */
      const uint8_t code = static_cast<uint8_t>(c);
      const uint8_t* glyph = ram + 0x5600 + code * 8;
      coord_t width = code < 128 ? ram[0x5600] : ram[0x5601];
      coord_t raise = 0;
      if ((ram[0x5605] & 0x01) && code >= 16)
      {
        const uint8_t adjust = (ram[0x5608 + (code - 16) / 2] >> ((code & 1) ? 4 : 0)) & 0x0f;
        width += (adjust & 0x07) >= 4 ? (adjust & 0x07) - 8 : (adjust & 0x07);
        if (adjust & 0x08) raise = 1;
      }
      const coord_t ox = static_cast<int8_t>(ram[0x5603]);
      const coord_t oy = static_cast<int8_t>(ram[0x5604]) - raise;

      for (coord_t ty = 0; ty < 8; ++ty)
        for (coord_t tx = 0; tx < 8; ++tx)
          if (glyph[ty] & (1 << tx))
            for (int py = 0; py < tall; ++py)
              for (int px = 0; px < wide; ++px)
                pset(x + (ox + tx) * wide + px, y + (oy + ty) * tall + py, color);

      x += width * wide;
      if (x > right) right = x;
      continue;
    }

    auto specialGlyph = static_cast<uint8_t>(c) < 0x80 ? SpecialGlyphs.end() : std::find_if(SpecialGlyphs.begin(), SpecialGlyphs.end(), [&string, &i](const SpecialGlyph& glyph) {
      return string.size() >= i + glyph.encoding.size() && memcmp(&string[i], &glyph.encoding[0], glyph.encoding.size()) == 0; //TODO: memcmp is not best design ever
    });

    const gfx::sequential_sprite_t* sprite = nullptr;
    coord_t width = gfx::GLYPH_WIDTH;

    if (specialGlyph != SpecialGlyphs.end())
    {
      sprite = _font.specialGlyph(specialGlyph->index);
      width = 8;
      i += specialGlyph->encoding.size() - 1;
    }
    else
      sprite = _font.glyph(c);

    if (sprite)
    {
      for (coord_t ty = 0; ty < gfx::GLYPH_HEIGHT; ++ty)
        for (coord_t tx = 0; tx < width; ++tx)
        {
          color_t fcolor = sprite->get(tx, ty);
          if (fcolor != 0)
            for (int py = 0; py < tall; ++py)
              for (int px = 0; px < wide; ++px)
                pset(x + tx * wide + px, y + ty * tall + py, color);
        }

      x += width * wide;
      if (x > right) right = x;
    }
  }
  return finish();
}

void Machine::pal(color_t c0, color_t c1, palette_index_t index)
{
  gfx::palette_t* palette = _memory.paletteAt(index);
  palette->set(c0, c1);
}


void Machine::map(coord_t cx, coord_t cy, coord_t x, coord_t y, amount_t cw, amount_t ch, sprite_flags_t layer)
{
  /* tiles wholly outside the camera and clip draw nothing, so the loop skips them */
  coord_t sx = x, sy = y, px0, py0, px1, py1;
  if (cw <= 0 || ch <= 0 || !visible(sx, sy, cw * gfx::SPRITE_WIDTH, ch * gfx::SPRITE_HEIGHT, px0, py0, px1, py1))
    return;

  const amount_t tx0 = px0 / gfx::SPRITE_WIDTH, tx1 = (px1 + gfx::SPRITE_WIDTH - 1) / gfx::SPRITE_WIDTH;
  const amount_t ty0 = py0 / gfx::SPRITE_HEIGHT, ty1 = (py1 + gfx::SPRITE_HEIGHT - 1) / gfx::SPRITE_HEIGHT;

  for (amount_t ty = ty0; ty < ty1; ++ty)
  {
    for (amount_t tx = tx0; tx < tx1; ++tx)
    {
      const coord_t mx = cx + tx, my = cy + ty;
      if (!_memory.inMap(mx, my))
        continue;

      const sprite_index_t index = *_memory.spriteInTileMap(mx, my);

      /* TODO: experimentally the behavior is layer & flags != 0 instead that layer & flags == layer */
      if ((index != 0 || (_memory.base()[address::DRAW_FLAGS] & 0x08)) &&
          (!layer || (layer & *_memory.spriteFlagsFor(index)) != 0))
        spr(index, x + tx * gfx::SPRITE_WIDTH, y + ty * gfx::SPRITE_HEIGHT);
    }
  }
}

namespace
{
  constexpr uint32_t STATE_MAGIC = 0x31533850; /* "P8S1" */

  void putChunk(std::string& out, const std::string& chunk)
  {
    const uint32_t length = uint32_t(chunk.size());
    out.append(reinterpret_cast<const char*>(&length), sizeof(length));
    out.append(chunk);
  }

  bool getChunk(const uint8_t*& at, const uint8_t* end, std::string& chunk)
  {
    uint32_t length = 0;
    if (size_t(end - at) < sizeof(length)) return false;
    std::memcpy(&length, at, sizeof(length));
    at += sizeof(length);
    if (size_t(end - at) < length) return false;
    chunk.assign(reinterpret_cast<const char*>(at), length);
    at += length;
    return true;
  }
}

bool Machine::saveState(std::string& out)
{
  std::string lua;
  if (!_code.saveState(lua))
    return false;

  std::ostringstream rnd;
  rnd << _state.rnd;

  out.clear();
  out.append(reinterpret_cast<const char*>(&STATE_MAGIC), sizeof(STATE_MAGIC));
  putChunk(out, std::string(reinterpret_cast<const char*>(_memory.base()), Memory::MEMORY_SIZE));
  putChunk(out, std::string(reinterpret_cast<const char*>(_memory.backup()), address::CART_DATA_LENGTH));
  putChunk(out, rnd.str());
  putChunk(out, std::string(reinterpret_cast<const char*>(&_state.lastLineEnd), sizeof(_state.lastLineEnd)));
  putChunk(out, lua::cartParam);
  putChunk(out, lua::cartBreadcrumb);

  std::string sound;
#if SOUND_ENABLED
  _sound.saveState(sound);
#endif
  putChunk(out, sound);
  putChunk(out, lua);
  return true;
}

bool Machine::loadState(const uint8_t* data, size_t size)
{
  const uint8_t* at = data;
  const uint8_t* end = data + size;

  uint32_t magic = 0;
  if (size < sizeof(magic)) return false;
  std::memcpy(&magic, at, sizeof(magic));
  at += sizeof(magic);
  if (magic != STATE_MAGIC) return false;

  std::string memory, backup, rnd, lineEnd, param, breadcrumb, sound, lua;
  if (!getChunk(at, end, memory) || !getChunk(at, end, backup) || !getChunk(at, end, rnd) || !getChunk(at, end, lineEnd) ||
      !getChunk(at, end, param) || !getChunk(at, end, breadcrumb) || !getChunk(at, end, sound) || !getChunk(at, end, lua))
    return false;

  if (memory.size() != size_t(Memory::MEMORY_SIZE) || backup.size() != size_t(address::CART_DATA_LENGTH) ||
      lineEnd.size() != sizeof(_state.lastLineEnd))
    return false;

  /* the Lua state goes first, as it is the part that can be refused */
  if (!_code.loadState(lua))
    return false;

  std::memcpy(_memory.base(), memory.data(), memory.size());
  std::memcpy(_memory.backupData(), backup.data(), backup.size());
  std::istringstream rndIn(rnd);
  rndIn >> _state.rnd;
  std::memcpy(&_state.lastLineEnd, lineEnd.data(), lineEnd.size());
  lua::cartParam = param;
  lua::cartBreadcrumb = breadcrumb;

#if SOUND_ENABLED
  if (!sound.empty())
    _sound.loadState(sound);
#endif
  return true;
}
