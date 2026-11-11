#include "cart.h"

#include "loader.h"
#include "stegano.h"
#include "vm/machine.h"
#include "vm/lua_bridge.h"

#include <cstdio>
#include <cstring>
#include <memory>

using namespace retro8;

std::string io::cartDirectory;

bool io::readFile(const std::string& path, std::vector<uint8_t>& out)
{
  FILE* file = std::fopen(path.c_str(), "rb");
  if (!file)
    return false;

  out.clear();
  uint8_t buffer[16384];
  size_t count;
  while ((count = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
    out.insert(out.end(), buffer, buffer + count);
  std::fclose(file);
  return true;
}

std::string io::findCart(const std::string& name)
{
  if (name.empty() || name[0] == '#')
    return std::string();

  const std::string base = name[0] == '/' || cartDirectory.empty() ? name : cartDirectory + "/" + name;
  for (const char* suffix : { "", ".p8", ".p8.png" })
  {
    const std::string path = base + suffix;
    if (FILE* file = std::fopen(path.c_str(), "rb"))
    {
      std::fclose(file);
      return path;
    }
  }

  return std::string();
}

bool io::loadCart(const uint8_t* data, size_t size, Machine& m, bool dataOnly)
{
  if (size > 4 && std::memcmp(data, "\x89PNG", 4) == 0)
  {
    std::vector<uint8_t> out;
    unsigned long width, height;
    if (Platform::loadPNG(out, width, height, data, size, true) != 0)
      return false;

    std::vector<uint32_t> pixels(out.size() / 4);
    for (size_t i = 0; i < pixels.size(); ++i)
      pixels[i] = out[4 * i] | (out[4 * i + 1] << 8) | (out[4 * i + 2] << 16) | (out[4 * i + 3] << 24);

    PngData png = { pixels.data(), nullptr, pixels.size() };
    Stegano stegano;
    return dataOnly ? stegano.loadRom(png, m) : stegano.load(png, m);
  }

  Loader loader;
  loader.dataOnly = dataOnly;
  return loader.loadRaw(std::string(reinterpret_cast<const char*>(data), size), m);
}

bool io::cartRom(const std::string& name, uint8_t* rom)
{
  /* level carts read their data a cart at a time, so the last one read is kept */
  static std::string cachedPath;
  static std::vector<uint8_t> cached;

  const std::string path = findCart(name);
  if (path.empty())
    return false;

  if (path != cachedPath)
  {
    std::vector<uint8_t> data;
    if (!readFile(path, data))
      return false;

    std::unique_ptr<Machine> temporary(new Machine());
    if (!loadCart(data.data(), data.size(), *temporary, true))
      return false;

    cached.assign(temporary->memory().base(), temporary->memory().base() + address::CART_DATA_LENGTH);
    cachedPath = path;
  }

  std::memcpy(rom, cached.data(), address::CART_DATA_LENGTH);
  return true;
}

bool io::switchCart(Machine*& m)
{
  const std::string path = lua::pendingCart;
  lua::pendingCart.clear();

  std::vector<uint8_t> data;
  if (path.empty() || !readFile(path, data))
    return false;

  lua::cartdataFlush();

  Machine* previous = m;
  m = machine = new Machine();
  m->font().load();
  m->code().loadAPI();
  lua::cartParam = lua::pendingParam;

  const size_t slash = path.find_last_of('/');
  if (slash != std::string::npos)
    cartDirectory = path.substr(0, slash);

  if (!loadCart(data.data(), data.size(), *m))
  {
    delete m;
    m = machine = previous;
    return false;
  }

  delete previous;

  if (m->code().hasInit())
    m->code().init();

  return true;
}
