#pragma once

#include "common.h"

#include <string>
#include <vector>

namespace retro8
{
  class Machine;

  namespace io
  {
    /* loads a .p8 or .p8.png cart into the machine, leaving its code out when dataOnly is set */
    bool loadCart(const uint8_t* data, size_t size, Machine& m, bool dataOnly = false);

    /* the folder of the running cart, which load(), reload() and #include read other files from */
    extern std::string cartDirectory;

    bool readFile(const std::string& path, std::vector<uint8_t>& out);

    /* the path load() would open for a cart name, trying it as given and then with .p8 and .p8.png, empty when none exists */
    std::string findCart(const std::string& name);

    /* copies the first 0x4300 bytes of another cart into rom */
    bool cartRom(const std::string& name, uint8_t* rom);

    /* replaces the machine with a fresh one running the cart that load() asked for, returns false and keeps the old one when
     * the cart cannot be read */
    bool switchCart(Machine*& m);
  }
}
