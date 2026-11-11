#include "loader.h"

#include "stegano.h"
#include "cart.h"

#include <fstream>
#include <algorithm>
#include <iterator>
#include <cassert>
#include <sstream>
//#include <cctype>

using namespace retro8;
using namespace retro8::io;

void Loader::fixLine(std::string& line)
{
  /* inline ? print operator */
  if (!line.empty() && line[0] == '?')
    line = "print(" + line.substr(1) + ")";
}

int Loader::valueForHexDigit(char c)
{
  int v = (c >= 'A') ? (c >= 'a') ? (c - 'a' + 10) : (c - 'A' + 10) : (c - '0');
  assert(v >= 0 && v <= 0xf);
  return v;
}

color_t Loader::colorFromDigit(char d)
{
  return static_cast<color_t>(valueForHexDigit(d));
}

sprite_index_t Loader::spriteIndexFromString(const char* c)
{
  int h = valueForHexDigit(c[0]);
  int l = valueForHexDigit(c[1]);
  return (h << 4) | l;
}

uint8_t Loader::valueForUint8(const char* c)
{
  int h = valueForHexDigit(c[0]);
  int l = valueForHexDigit(c[1]);
  return (h << 4) | l;
}

retro8::sprite_flags_t Loader::spriteFlagsFromString(const char* c)
{
  int h = valueForHexDigit(c[0]);
  int l = valueForHexDigit(c[1]);
  return (h << 4) | l;
}

bool Loader::isPngCartridge(const std::string& path)
{
  return path.length() >= 4 && path.substr(path.length() - 4) == ".png";
}

template<typename T>
std::vector<std::string> Loader::loadLines(T& input)
{
  std::vector<std::string> lines;

  for (std::string line; std::getline(input, line); /**/)
    lines.push_back(line);

  for (auto& line : lines)
    if (!line.empty() && line.back() == '\r')
      line.resize(line.length() - 1);

  return lines;
}

void Loader::loadFile(const std::string& path, Machine& dest)
{
  std::ifstream stream(path);
  assert(stream.good());
  load(loadLines(stream), dest);
}

/* #include pastes a file from the cart's folder in place of the line, and included files can include others */
void Loader::include(std::string name, std::stringstream& code, int depth)
{
  while (!name.empty() && (name.back() == ' ' || name.back() == '\t'))
    name.pop_back();

  std::vector<uint8_t> data;
  const std::string path = name[0] == '/' || cartDirectory.empty() ? name : cartDirectory + "/" + name;
  if (depth > 8 || !readFile(path, data))
  {
    code << "-- #include " << name << " not found" << std::endl;
    return;
  }

  std::stringstream stream(std::string(data.begin(), data.end()));
  for (const std::string& line : loadLines(stream))
  {
    if (line.compare(0, 9, "#include ") == 0)
      include(line.substr(9), code, depth + 1);
    else
      code << line << std::endl;
  }
}

/* .p8 files keep the code as UTF-8, writing each P8SCII character above ASCII as a glyph; PICO-8 works on single bytes, so the
 * code is turned back into them, which makes #"■" one character and ord("█") 128 as in PNG carts */
static const char* const P8SCII[256] = {
  "\x00", "\xc2\xb9", "\xc2\xb2", "\xc2\xb3", "\xe2\x81\xb4", "\xe2\x81\xb5", "\xe2\x81\xb6", "\xe2\x81\xb7",
  "\xe2\x81\xb8", "\x09", "\x0a", "\xe1\xb5\x87", "\xe1\xb6\x9c", "\x0d", "\xe1\xb5\x89", "\xe1\xb6\xa0",
  "\xe2\x96\xae", "\xe2\x96\xa0", "\xe2\x96\xa1", "\xe2\x81\x99", "\xe2\x81\x98", "\xe2\x80\x96", "\xe2\x97\x80", "\xe2\x96\xb6",
  "\xe3\x80\x8c", "\xe3\x80\x8d", "\xc2\xa5", "\xe2\x80\xa2", "\xe3\x80\x81", "\xe3\x80\x82", "\xe3\x82\x9b", "\xe3\x82\x9c",
  "\x20", "\x21", "\x22", "\x23", "\x24", "\x25", "\x26", "\x27",
  "\x28", "\x29", "\x2a", "\x2b", "\x2c", "\x2d", "\x2e", "\x2f",
  "\x30", "\x31", "\x32", "\x33", "\x34", "\x35", "\x36", "\x37",
  "\x38", "\x39", "\x3a", "\x3b", "\x3c", "\x3d", "\x3e", "\x3f",
  "\x40", "\x41", "\x42", "\x43", "\x44", "\x45", "\x46", "\x47",
  "\x48", "\x49", "\x4a", "\x4b", "\x4c", "\x4d", "\x4e", "\x4f",
  "\x50", "\x51", "\x52", "\x53", "\x54", "\x55", "\x56", "\x57",
  "\x58", "\x59", "\x5a", "\x5b", "\x5c", "\x5d", "\x5e", "\x5f",
  "\x60", "\x61", "\x62", "\x63", "\x64", "\x65", "\x66", "\x67",
  "\x68", "\x69", "\x6a", "\x6b", "\x6c", "\x6d", "\x6e", "\x6f",
  "\x70", "\x71", "\x72", "\x73", "\x74", "\x75", "\x76", "\x77",
  "\x78", "\x79", "\x7a", "\x7b", "\x7c", "\x7d", "\x7e", "\xe2\x97\x8b",
  "\xe2\x96\x88", "\xe2\x96\x92", "\xf0\x9f\x90\xb1", "\xe2\xac\x87\xef\xb8\x8f", "\xe2\x96\x91", "\xe2\x9c\xbd", "\xe2\x97\x8f", "\xe2\x99\xa5",
  "\xe2\x98\x89", "\xec\x9b\x83", "\xe2\x8c\x82", "\xe2\xac\x85\xef\xb8\x8f", "\xf0\x9f\x98\x90", "\xe2\x99\xaa", "\xf0\x9f\x85\xbe\xef\xb8\x8f", "\xe2\x97\x86",
  "\xe2\x80\xa6", "\xe2\x9e\xa1\xef\xb8\x8f", "\xe2\x98\x85", "\xe2\xa7\x97", "\xe2\xac\x86\xef\xb8\x8f", "\xcb\x87", "\xe2\x88\xa7", "\xe2\x9d\x8e",
  "\xe2\x96\xa4", "\xe2\x96\xa5", "\xe3\x81\x82", "\xe3\x81\x84", "\xe3\x81\x86", "\xe3\x81\x88", "\xe3\x81\x8a", "\xe3\x81\x8b",
  "\xe3\x81\x8d", "\xe3\x81\x8f", "\xe3\x81\x91", "\xe3\x81\x93", "\xe3\x81\x95", "\xe3\x81\x97", "\xe3\x81\x99", "\xe3\x81\x9b",
  "\xe3\x81\x9d", "\xe3\x81\x9f", "\xe3\x81\xa1", "\xe3\x81\xa4", "\xe3\x81\xa6", "\xe3\x81\xa8", "\xe3\x81\xaa", "\xe3\x81\xab",
  "\xe3\x81\xac", "\xe3\x81\xad", "\xe3\x81\xae", "\xe3\x81\xaf", "\xe3\x81\xb2", "\xe3\x81\xb5", "\xe3\x81\xb8", "\xe3\x81\xbb",
  "\xe3\x81\xbe", "\xe3\x81\xbf", "\xe3\x82\x80", "\xe3\x82\x81", "\xe3\x82\x82", "\xe3\x82\x84", "\xe3\x82\x86", "\xe3\x82\x88",
  "\xe3\x82\x89", "\xe3\x82\x8a", "\xe3\x82\x8b", "\xe3\x82\x8c", "\xe3\x82\x8d", "\xe3\x82\x8f", "\xe3\x82\x92", "\xe3\x82\x93",
  "\xe3\x81\xa3", "\xe3\x82\x83", "\xe3\x82\x85", "\xe3\x82\x87", "\xe3\x82\xa2", "\xe3\x82\xa4", "\xe3\x82\xa6", "\xe3\x82\xa8",
  "\xe3\x82\xaa", "\xe3\x82\xab", "\xe3\x82\xad", "\xe3\x82\xaf", "\xe3\x82\xb1", "\xe3\x82\xb3", "\xe3\x82\xb5", "\xe3\x82\xb7",
  "\xe3\x82\xb9", "\xe3\x82\xbb", "\xe3\x82\xbd", "\xe3\x82\xbf", "\xe3\x83\x81", "\xe3\x83\x84", "\xe3\x83\x86", "\xe3\x83\x88",
  "\xe3\x83\x8a", "\xe3\x83\x8b", "\xe3\x83\x8c", "\xe3\x83\x8d", "\xe3\x83\x8e", "\xe3\x83\x8f", "\xe3\x83\x92", "\xe3\x83\x95",
  "\xe3\x83\x98", "\xe3\x83\x9b", "\xe3\x83\x9e", "\xe3\x83\x9f", "\xe3\x83\xa0", "\xe3\x83\xa1", "\xe3\x83\xa2", "\xe3\x83\xa4",
  "\xe3\x83\xa6", "\xe3\x83\xa8", "\xe3\x83\xa9", "\xe3\x83\xaa", "\xe3\x83\xab", "\xe3\x83\xac", "\xe3\x83\xad", "\xe3\x83\xaf",
  "\xe3\x83\xb2", "\xe3\x83\xb3", "\xe3\x83\x83", "\xe3\x83\xa3", "\xe3\x83\xa5", "\xe3\x83\xa7", "\xe2\x97\x9c", "\xe2\x97\x9d",
};

static std::string toP8scii(const std::string& in)
{
  struct Glyph { std::string utf8; uint8_t code; };
  static std::vector<Glyph> byLead[256];
  static bool built = false;

  if (!built)
  {
    for (int c = 1; c < 256; ++c)
    {
      if (c == 9 || c == 10 || c == 13 || (c >= 32 && c < 127))
        continue;
      std::string utf8 = P8SCII[c];
      byLead[(uint8_t)utf8[0]].push_back({ utf8, uint8_t(c) });
      /* some editors drop the emoji variation selector */
      const std::string selector = "\xef\xb8\x8f";
      if (utf8.size() > selector.size() && utf8.compare(utf8.size() - selector.size(), selector.size(), selector) == 0)
        byLead[(uint8_t)utf8[0]].push_back({ utf8.substr(0, utf8.size() - selector.size()), uint8_t(c) });
    }
    for (auto& list : byLead)
      std::sort(list.begin(), list.end(), [](const Glyph& a, const Glyph& b) { return a.utf8.size() > b.utf8.size(); });
    built = true;
  }

  std::string out;
  out.reserve(in.size());

  for (size_t i = 0; i < in.size();)
  {
    const uint8_t lead = (uint8_t)in[i];
    bool matched = false;

    if (lead >= 0x80)
      for (const Glyph& glyph : byLead[lead])
        if (in.compare(i, glyph.utf8.size(), glyph.utf8) == 0)
        {
          out.push_back((char)glyph.code);
          i += glyph.utf8.size();
          matched = true;
          break;
        }

    if (!matched)
      out.push_back(in[i++]);
  }

  return out;
}

bool Loader::loadRaw(const std::string& data, Machine& dest)
{
  std::stringstream stream(data);
  return load(loadLines(stream), dest);
}

bool Loader::load(const std::vector<std::string>& lines, Machine& m)
{ 
  enum class State { HEADER, CODE, GFX, GFF, LABEL, MAP, SFX, MUSIC };

  State state = State::HEADER;

  //TODO: not efficient but for now it's fine
  std::stringstream code;

  coord_t sy = 0, my = 0, fy = 0, snd = 0, msc = 0;

  static constexpr size_t DIGITS_PER_PIXEL_ROW = 128;
  static constexpr size_t BYTES_PER_GFX_ROW = DIGITS_PER_PIXEL_ROW / 2;

  static constexpr size_t DIGITS_PER_MAP_ROW = 256;
  static constexpr size_t DIGITS_PER_SPRITE_FLAGS_ROW = 128*2;

  static constexpr size_t DIGITS_PER_SOUND = 168;
  static constexpr size_t DIGITS_PER_MUSIC_PATTERN = 2 + 1 + 8;


  for (auto& line : lines)
  {
    /* change state according to */
    if (line == "__lua__")
      state = State::CODE;
    else if (line == "__gfx__")
      state = State::GFX;
    else if (line == "__gff__")
      state = State::GFF;
    else if (line == "__label__")
      state = State::LABEL;
    else if (line == "__map__")
      state = State::MAP;
    else if (line == "__sfx__")
      state = State::SFX;
    else if (line == "__music__")
    {
      /* patterns the cart leaves out are empty, with every channel switched off */
#if SOUND_ENABLED
      for (size_t i = 0; i < sfx::MUSIC_COUNT; ++i)
        for (sfx::channel_index_t c = 0; c < sfx::APU::CHANNEL_COUNT; ++c)
          m.memory().music(i)->setSound(c, 0x41 + c);
#endif
      state = State::MUSIC;
    }
    else if (line.empty()) /* skip empty lines */
      continue;
    /*else if (std::all_of(line.begin(), line.end(), [](char c) { return std::isspace(c); }))
      continue;*/
    else
    {
      switch (state)
      {
      case State::CODE:
        if (line.compare(0, 9, "#include ") == 0)
          include(line.substr(9), code, 0);
        else
          code << line << std::endl;
        break;

      case State::GFX:
        assert(line.length() == DIGITS_PER_PIXEL_ROW);
        for (coord_t x = 0; x < BYTES_PER_GFX_ROW; ++x)
        {
          const char* pair = line.c_str() + x * 2;
          auto* dest = m.memory().as<gfx::color_byte_t>(address::SPRITE_SHEET + sy * BYTES_PER_GFX_ROW + x);

          dest->setBoth(colorFromDigit(pair[0]), colorFromDigit(pair[1]));
        }

        ++sy;
        break;

      case State::MAP:
      {
        assert(line.length() == DIGITS_PER_MAP_ROW);
        for (coord_t x = 0; x < gfx::TILE_MAP_WIDTH; ++x)
        {
          const char* index = line.c_str() + x * 2;
          sprite_index_t sindex = spriteIndexFromString(index);
          *m.memory().spriteInTileMap(x, my) = sindex;
        }
        ++my;
        break;
      }
      case State::GFF:
      {
        assert(line.length() == DIGITS_PER_SPRITE_FLAGS_ROW);
        for (coord_t x = 0; x < DIGITS_PER_SPRITE_FLAGS_ROW/2; ++x)
        {
          const char* sflags = line.c_str() + x * 2;
          sprite_flags_t flags = spriteFlagsFromString(sflags);
          *m.memory().spriteFlagsFor(128*fy + x) = flags;
        }
        ++fy;
        break;
      }
#if SOUND_ENABLED
      case State::SFX:
      {
        assert(line.length() == DIGITS_PER_SOUND);
        const char* p = line.c_str();

        sfx::Sound* sound = m.memory().sound(snd);
        sound->flags = valueForUint8(p);
        sound->speed = valueForUint8(p+2);
        sound->loopStart = valueForUint8(p+4);
        sound->loopEnd = valueForUint8(p+6);

        p = p + 8;

        for (size_t i = 0; i < sound->samples.size(); ++i)
        {
          const char* s = p + (i * 5);
          
          auto& sample = sound->samples[i];
          
          sample.setPitch(valueForUint8(s));
          /* waveforms 8 to f play sfx 0 to 7 as the instrument */
          const int waveform = valueForHexDigit(s[2]);
          sample.setWaveform(sfx::Waveform(waveform & 7));
          sample.setCustom(waveform & 8);
          sample.setVolume(valueForHexDigit(s[3]));
          sample.setEffect(sfx::Effect(valueForHexDigit(s[4])));
        }

        ++snd;
        break;

      }
      case State::MUSIC:
      {
        const char* p = line.c_str();

        if (!line.empty())
        {
          sfx::Music* music = m.memory().music(msc);

          /* XX AABBCCDD, where a channel byte of 0x40 or more is switched off */
          uint8_t flags = valueForUint8(p);

          for (sfx::channel_index_t i = 0; i < sfx::APU::CHANNEL_COUNT; ++i)
            music->setSound(i, valueForUint8(p + 3 + 2 * i));

          /* bit n of the flags is bit 7 of byte n: loop start, loop back, stop, and a fourth that carts may use for data */
          uint8_t* bytes = m.memory().base() + address::MUSIC + msc * sizeof(sfx::Music);
          for (int i = 0; i < 4; ++i)
            if (flags & (1 << i))
              bytes[i] |= 0x80;

          ++msc;

        }
        
        break;
      }
#endif
      }

    }
  }

  /* reload() restores the cart as it was before any of its code ran */
  m.memory().backupCartridge();
  if (dataOnly)
    return true;
  return m.code().initFromSource(toP8scii(code.str()));
}
