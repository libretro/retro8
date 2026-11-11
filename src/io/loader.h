#include "common.h"

#include "vm/machine.h"

#include <vector>
#include <string>
#include <sstream>

namespace retro8
{
  namespace io
  {
    class Loader
    {
    private:
      int valueForHexDigit(char c);
      retro8::color_t colorFromDigit(char d);
      retro8::sprite_index_t spriteIndexFromString(const char* c);
      retro8::sprite_flags_t spriteFlagsFromString(const char* c);
      uint8_t valueForUint8(const char* c);

      template<typename T> std::vector<std::string> loadLines(T& stream);
      bool load(const std::vector<std::string>& lines, Machine& dest);
      void include(std::string name, std::stringstream& code, int depth);

    public:
      /* skip the code, for reading another cart's data */
      bool dataOnly = false;

      bool loadRaw(const std::string& data, Machine& dest);
      void loadFile(const std::string& path, Machine& dest);

      static bool isPngCartridge(const std::string& path);

      static void fixLine(std::string& line);

      
    };
  }
}