#include "stegano.h"

#include <cassert>
#include <algorithm>

using namespace retro8;
using namespace io;

constexpr size_t RAW_DATA_LENGTH = 0x4300;
constexpr size_t MAGIC_LENGTH = 4;
constexpr size_t HEADER_20_LENGTH = 8;

#if DEBUGGER
#include <fstream>
static std::string fileName = "foo.p8";
#endif

uint8_t Stegano::assembleByte(const uint32_t v)
{
  constexpr uint32_t MASK_ALPHA = 0xff000000;
  constexpr uint32_t MASK_RED = 0x00ff0000;
  constexpr uint32_t MASK_GREEN = 0x0000ff00;
  constexpr uint32_t MASK_BLUE = 0x000000ff;

  constexpr uint32_t SHIFT_ALPHA = 24;
  constexpr uint32_t SHIFT_RED = 16;
  constexpr uint32_t SHIFT_GREEN = 8;
  constexpr uint32_t SHIFT_BLUE = 0;

  return
    ((((v & MASK_ALPHA) >> SHIFT_ALPHA) & 0b11) << 6) |
    ((((v & MASK_RED) >> SHIFT_RED) & 0b11) << 0) |
    ((((v & MASK_GREEN) >> SHIFT_GREEN) & 0b11) << 2) |
    ((((v & MASK_BLUE) >> SHIFT_BLUE) & 0b11) << 4);
}



class PXADecoder
{
public:
  const uint8_t* data;
  size_t length;
  size_t b; /* bit index */
  size_t o; /* byte index */
  size_t expected;
  bool failed;
  std::array<uint8_t, 256> m;

private:
  bool readBit()
  {
    if (o >= length)
    {
      failed = true;
      return false;
    }

    int v = data[o] & (1 << b);
    ++b;

    if (b == 8)
    {
      b = 0;
      ++o;
    }

    return v;
  }

  int32_t readBits(size_t c)
  {
    int32_t r = 0;
    for (size_t i = 0; i < c; ++i)
      if (readBit())
        r |= 1 << i;

    return r;
  }

  void moveToFront(size_t i)
  {
    int v = m[i];
    for (int j = i; j > 0; --j)
      m[j] = m[j - 1];
    m[0] = v;
  }

public:
  PXADecoder(const uint8_t* data, size_t length, size_t expected) : data(data), length(length), b(0), o(0), expected(expected), failed(false)
  {
    /* initalize mapping, each value to itself */
    //TODO
    for (size_t i = 0; i < m.size(); ++i)
      m[i] = i;
  }

  std::string process()
  {
    std::string code;
    while (code.size() < expected && !failed)
    {
      bool h = readBit();

      /* read index to move to front, and output value */
      if (h == 1)
      {
        int unary = 0;
        while (readBit())
          ++unary;

        uint8_t unaryMask = ((1 << unary) - 1);
        uint8_t index = readBits(4 + unary) + (unaryMask << 4);

        code += m[index];
        moveToFront(index);
      }
      /* copy section */
      else
      {
        /* read offset */
        int32_t offsetBits;

        if (readBit())
          offsetBits = readBit() ? 5 : 10;
        else
          offsetBits = 15;

        auto offset = readBits(offsetBits) + 1;

        /* special hacky case in which bytes are directly emitted without
           affecting move-to-front
         */
        if (offsetBits == 10 && offset == 1)
        {
          uint8_t v = readBits(8);
          while (v && !failed)
          {
            code += v;
            v = readBits(8);
          }
        }
        else
        {
          /* read length */
          int32_t length = 3, part = 0;
          do
          {
            part = readBits(3);
            length += part;
          } while (part == 0b111 && !failed);

          /* a damaged image refers back before the start of the code */
          if (offset > code.size())
          {
            failed = true;
            break;
          }

          size_t start = code.size() - offset;
          for (int32_t l = 0; l < length; ++l)
          {
            code += code[start + l];
          }
        }
      }

    }

    return code;
  }

};


bool Stegano::load20(const PngData& data, Machine& m)
{
  auto* d = data.data;
  size_t o = RAW_DATA_LENGTH + MAGIC_LENGTH;

  size_t decompressedLength = assembleByte(d[o]) << 8 | assembleByte(d[o+1]);
  size_t compressedLength = assembleByte(d[o+2]) << 8 | assembleByte(d[o+3]);
  o += 4;

  if (compressedLength < HEADER_20_LENGTH)
    return false;
  compressedLength -= HEADER_20_LENGTH; /* subtract header length */

  compressedLength = std::min(data.length - o, compressedLength);

  std::vector<uint8_t> assembled(compressedLength);
  std::generate(assembled.begin(), assembled.end(), [this, &d, &o] () { return assembleByte(d[o++]); });

  auto decoder = PXADecoder(assembled.data(), assembled.size(), decompressedLength);
  std::string code = decoder.process();
  if (decoder.failed)
    return false;

#if DEBUGGER
  std::ofstream output(fileName);
  output << code;
  output.close();
#endif

  return m.code().initFromSource(code);
}

bool Stegano::load10(const PngData& data, Machine& m)
{
  auto* d = data.data;
  size_t o = RAW_DATA_LENGTH + MAGIC_LENGTH;

  /* read uint6_t msb compressed length*/
  size_t compressedLength = assembleByte(d[o]) << 8 | assembleByte(d[o + 1]);
  o += 2;
  /* skip 2 null*/
  o += 2;

  compressedLength = std::min(data.length - o, compressedLength);

  const std::string lookup = "\n 0123456789abcdefghijklmnopqrstuvwxyz!#%(){}[]<>+=/*:;.,~_";
  std::string code;

  assert(0x3b == lookup.length());
  //TODO: optimize concatenation on string by reserving space

  for (size_t i = 0; i < compressedLength; ++i)
  {
    uint8_t v = assembleByte(d[o + i]);

    /* copy next */
    if (v != 0x00 && v > 0x3b && i + 1 >= compressedLength)
      return false;

    if (v == 0x00)
    {
      if (i + 1 >= compressedLength)
        break;
      uint8_t vn = assembleByte(d[o + i + 1]);
      code += vn;
      ++i;
    }
    /* lookup */
    else if (v <= 0x3b)
    {
      code += lookup[v - 1];
    }
    else
    {
      uint8_t vn = assembleByte(d[o + i + 1]);

      auto offset = ((v - 0x3c) << 4) + (vn & 0xf);
      auto length = (vn >> 4) + 2;

      if ((size_t)offset > code.length())
        return false;

      const size_t start = code.length() - offset;
      for (size_t j = 0; j < length; ++j)
        code += code[start + j];

      ++i;
    }
  }

#if DEBUGGER
  std::ofstream output(fileName);
  output << code;
  output.close();
#endif

  return m.code().initFromSource(code);
}

/* without a compression header the code is plain text, ending at the first zero */
bool Stegano::loadPlain(const PngData& data, Machine& m)
{
  std::string code;
  for (size_t i = RAW_DATA_LENGTH; i < data.length && i < 0x8000; ++i)
  {
    const uint8_t v = assembleByte(data.data[i]);
    if (v == 0)
      break;
    code += (char)v;
  }

  return m.code().initFromSource(code);
}

bool Stegano::loadRom(const PngData& data, Machine& m)
{
  if (data.length != IMAGE_WIDTH * IMAGE_HEIGHT)
    return false;

  for (size_t i = 0; i < address::CART_DATA_LENGTH; ++i)
    m.memory().base()[i] = assembleByte(data.data[i]);

  m.memory().backupCartridge();
  return true;
}

bool Stegano::load(const PngData& data, Machine& m)
{
  constexpr size_t SPRITE_SHEET_SIZE = gfx::SPRITE_SHEET_HEIGHT * gfx::SPRITE_SHEET_WIDTH / gfx::PIXEL_TO_BYTE_RATIO;
  constexpr size_t TILE_MAP_SIZE = gfx::TILE_MAP_WIDTH * gfx::TILE_MAP_HEIGHT * sizeof(sprite_index_t) / 2;
  constexpr size_t SPRITE_FLAGS_SIZE = gfx::SPRITE_COUNT * sizeof(sprite_flags_t);
#if SOUND_ENABLED
  constexpr size_t MUSIC_SIZE = sfx::MUSIC_COUNT * sizeof(sfx::music_t);
  constexpr size_t SOUND_SIZE = sfx::SOUND_COUNT * sizeof(sfx::sound_t);

  static_assert(sizeof(sfx::music_t) == 4, "Must be 4 bytes");
  static_assert(sizeof(sfx::sound_t) == 68, "Must be 68 bytes");

  static_assert(RAW_DATA_LENGTH == SPRITE_SHEET_SIZE + TILE_MAP_SIZE + SPRITE_FLAGS_SIZE + MUSIC_SIZE + SOUND_SIZE, "Must be equal");
#endif
  if (data.length != IMAGE_WIDTH * IMAGE_HEIGHT)
    return false;

  auto* d = data.data;

  /* first 0x4300 are read directly into the cart */
  for (size_t i = 0; i < RAW_DATA_LENGTH; ++i)
    m.memory().base()[i] = assembleByte(d[i]);

  /* reload() restores the cart as it was before any of its code ran */
  m.memory().backupCartridge();

  size_t o = RAW_DATA_LENGTH;
  std::array<uint8_t, MAGIC_LENGTH> magic;

  /* two different magic numbers according to version */
  std::array<uint8_t, MAGIC_LENGTH> expected = { { ':', 'c', ':', '\0' } };
  std::array<uint8_t, MAGIC_LENGTH> expected2 = { { '\0', 'p', 'x', 'a' } };

  /* read magic code heaader */
  for (size_t i = 0; i < magic.size(); ++i)
    magic[i] = assembleByte(d[o++]);

  /* use different algorithms according to cartridge version */
  if (magic == expected)
    return load10(data, m);
  if (magic == expected2)
    return load20(data, m);
  return loadPlain(data, m);


}
