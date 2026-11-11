#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif
#include "libretro.h"

#include "common.h"
#include "vm/gfx.h"

#include "io/loader.h"
#include "io/stegano.h"
#include "io/cart.h"
#include "vm/machine.h"
#include "vm/input.h"

#include <stdio.h>
#include <cstring>
#include <string>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <algorithm>

#define LIBRETRO_LOG(x, ...) env.logger(retro_log_level::RETRO_LOG_INFO, x # __VA_ARGS__)

namespace r8 = retro8;

constexpr int SAMPLE_RATE = 44100;
constexpr int SAMPLES_PER_FRAME = SAMPLE_RATE / 60;
constexpr int SOUND_CHANNELS = 2;

r8::Machine *machine;
r8::io::Loader loader;

r8::input::InputManager input;
template <typename pixel_t>
class Screen {
public:
  ~Screen() {
    delete[] buffer;
    delete[] overlay;
  }

  /* the 16 colours a source line shows, which can differ line to line through 0x5f5f */
  void lineColours(r8::Memory& memory, int y, pixel_t* colours) {
    for (int c = 0; c < 16; ++c) {
      const uint8_t entry = memory.screenEntry(r8::color_t(c), y);
      colours[c] = colorTable.get((entry & 0x0f) | ((entry & 0x80) >> 3));
    }
  }

  /* 0x5f2c: 1 to 3 stretch the left or top half, 5 to 7 mirror it, 129 to 131 flip, 133 to 135 rotate by 90, 180 and 270 */
  void draw(const r8::gfx::color_byte_t *data, r8::Memory& memory, uint8_t mode) {
    auto pointer = buffer;
    pixel_t colours[128][16];
    for (int y = 0; y < 128; ++y)
      lineColours(memory, y, colours[y]);

    if (mode != 0) {
      for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 128; ++x) {
          int sx = x, sy = y;
          switch (mode) {
          case 1: sx = x / 2; break;
          case 2: sy = y / 2; break;
          case 3: sx = x / 2; sy = y / 2; break;
          case 5: sx = x < 64 ? x : 127 - x; break;
          case 6: sy = y < 64 ? y : 127 - y; break;
          case 7: sx = x < 64 ? x : 127 - x; sy = y < 64 ? y : 127 - y; break;
          case 129: sx = 127 - x; break;
          case 130: sy = 127 - y; break;
          case 131: sx = 127 - x; sy = 127 - y; break;
          case 133: sx = y; sy = 127 - x; break;
          case 134: sx = 127 - x; sy = 127 - y; break;
          case 135: sx = 127 - y; sy = x; break;
          default: break;
          }
          const r8::gfx::color_byte_t* pixels = data + sy * 64 + sx / 2;
          *pointer++ = colours[sy][(sx & 1) ? pixels->high() : pixels->low()];
        }
      }
      return;
    }

    for (size_t i = 0; i < r8::gfx::BYTES_PER_SCREEN; ++i) {
      const r8::gfx::color_byte_t* pixels = data + i;
      const pixel_t* line = colours[i / 64];

      *(pointer) = line[pixels->low()];
      *((pointer)+1) = line[pixels->high()];
      (pointer) += 2;
    }
  }

  const pixel_t *getBuffer() {
    return buffer;
  }

  /* the frame with an arrow on top, kept apart from the buffer because a 30 fps cart leaves the buffer up for two frames */
  const pixel_t *getBufferWithCursor(int x, int y) {
    static const char* const arrow[] = { "b", "bb", "bwb", "bwwb", "bwwwb", "bwbbb", "bb" };
    std::memcpy(overlay, buffer, sizeof(pixel_t) * r8::gfx::SCREEN_WIDTH * r8::gfx::SCREEN_HEIGHT);
    for (int row = 0; row < 7; ++row)
      for (int column = 0; arrow[row][column]; ++column) {
        const int px = x + column, py = y + row;
        if (px < 0 || py < 0 || px >= int(r8::gfx::SCREEN_WIDTH) || py >= int(r8::gfx::SCREEN_HEIGHT))
          continue;
        overlay[py * r8::gfx::SCREEN_WIDTH + px] = colorTable.get(arrow[row][column] == 'w' ? 7 : 0);
      }
    return overlay;
  }
protected:
  Screen(): buffer(new pixel_t[r8::gfx::SCREEN_WIDTH * r8::gfx::SCREEN_HEIGHT]), overlay(new pixel_t[r8::gfx::SCREEN_WIDTH * r8::gfx::SCREEN_HEIGHT]) {
  }

protected:
  r8::gfx::ColorTable colorTable;
private:
  pixel_t *buffer;
  pixel_t *overlay;
};

struct Screen32 : Screen<uint32_t> {
public:
  Screen32() {
    colorTable.init(ColorMapper32());
  }
private:
  struct ColorMapper32
  {
    r8::gfx::ColorTable::pixel_t operator()(uint8_t r, uint8_t g, uint8_t b) const { return 0xff000000 | (r << 16) | (g << 8) | b; }
  };
};

struct Screen16 : Screen<uint16_t> {
public:
  Screen16() {
    colorTable.init(ColorMapper16());
  }
private:
  struct ColorMapper16
  {
    r8::gfx::ColorTable::pixel_t operator()(uint8_t r, uint8_t g, uint8_t b) const {
#ifdef ABGR1555
      return ((b & 0xf8) << 7) | ((g & 0xf8) << 2) | ((r & 0xf8) >> 3);
#else
      return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | ((b & 0xf8) >> 3);
#endif
    }
  };
};

Screen16 *screen16;
Screen32 *screen32;
int16_t* audioBuffer;

static void fallback_log(enum retro_log_level level, const char *fmt, ...)
{
  (void)level;
  va_list va;
  va_start(va, fmt);
  vfprintf(stderr, fmt, va);
  va_end(va);
}

struct RetroArchEnv
{
  retro_video_refresh_t video;
  retro_audio_sample_t audio;
  retro_audio_sample_batch_t audioBatch;
  retro_input_poll_t inputPoll;
  retro_input_state_t inputState;
  retro_log_printf_t logger = fallback_log;
  retro_environment_t retro_cb;

  uint32_t frameCounter;
  uint16_t buttonState;
  bool buttonStates[retro8::PLAYER_COUNT][retro8::BUTTON_COUNT] = {};
  bool isRGB32;
};

RetroArchEnv env;

/* Carts that poke(0x5f2d, 1) read a mouse through stat(32) onwards. A real mouse or pointer from the frontend drives it, so
   does a stick, and holding Y turns the d-pad into the cursor for devices without sticks. L1 and R1 are the left and right
   clicks and either L2 or R2 the middle one. Nothing changes for a cart that never turns the mouse on. */
namespace mouse
{
  enum class Source { AUTO, RIGHT, LEFT, DPAD, OFF };

  struct Options
  {
    Source source = Source::AUTO;
    float speed = 128.0f;
    float deadzone = 0.2f;
    unsigned hold = RETRO_DEVICE_ID_JOYPAD_Y;
    bool cursor = false;
    bool centre = false;
  };

  struct Cursor
  {
    float x = 64.0f, y = 64.0f;
    float movedX = 0.0f, movedY = 0.0f;
    int32_t wheel = 0;
    uint8_t buttons = 0;
    bool holding = false;
    int32_t dpadFrames = 0;
    bool announced = false;
  };

  Options options;
  Cursor cursor;

  static const retro_core_option_definition definitions[] = {
    { "retro8_mouse_source", "Mouse Control",
      "What moves the mouse for carts that use one. Auto takes either stick and also lets the hold button turn the D-Pad into the cursor.",
      { { "auto", "Auto" }, { "right", "Right Stick" }, { "left", "Left Stick" }, { "dpad", "D-Pad Hold" }, { "off", "Disabled" }, { NULL, NULL } },
      "auto" },
    { "retro8_mouse_speed", "Mouse Speed", "How far the cursor travels in a second with the stick pushed all the way.",
      { { "64", "Slow" }, { "128", "Normal" }, { "192", "Fast" }, { "256", "Very Fast" }, { NULL, NULL } },
      "128" },
    { "retro8_mouse_deadzone", "Mouse Stick Deadzone", "How far a stick moves before the cursor follows it.",
      { { "10", "10%" }, { "15", "15%" }, { "20", "20%" }, { "25", "25%" }, { "30", "30%" }, { NULL, NULL } },
      "20" },
    { "retro8_mouse_hold", "Mouse Hold Button",
      "While held, the D-Pad moves the cursor and O and X also click left and right.",
      { { "y", "Y" }, { "select", "Select" }, { NULL, NULL } },
      "y" },
    { "retro8_mouse_cursor", "Mouse Cursor",
      "Shows an arrow at the mouse for carts that do not draw their own, or hides it. A cart that draws its own pointer keeps it.",
      { { "off", "Hidden" }, { "on", "Shown" }, { NULL, NULL } },
      "off" },
    { "retro8_mouse_function", "Mouse Function",
      "Free Movement leaves the cursor where it is moved to. Return to Centre places it out from the middle of the screen as far as the stick is pushed, and glides it back to the middle when the stick and D-Pad are let go.",
      { { "free", "Free Movement" }, { "centre", "Return to Centre" }, { NULL, NULL } },
      "free" },
    { "retro8_keyboard_dpad", "Keyboard D-Pad",
      "For carts that read the keyboard, the D-Pad also holds these keys.",
      { { "arrows", "Arrow Keys" }, { "wasd", "WASD" }, { "esdf", "ESDF" }, { "ijkl", "IJKL" }, { "off", "Disabled" }, { NULL, NULL } },
      "arrows" },
    { "retro8_keyboard_key", "Keyboard X Button",
      "For carts that read the keyboard, the X button presses this key. O and X still press Z and X.",
      { { "space", "Space" }, { "enter", "Enter" }, { "shift", "Shift" }, { "tab", "Tab" }, { "escape", "Escape" }, { "off", "Disabled" }, { NULL, NULL } },
      "space" },
    { NULL, NULL, NULL, { { NULL, NULL } }, NULL },
  };

  static const retro_variable legacy[] = {
    { "retro8_mouse_source", "Mouse Control; auto|right|left|dpad|off" },
    { "retro8_mouse_speed", "Mouse Speed; 128|192|256|64" },
    { "retro8_mouse_deadzone", "Mouse Stick Deadzone; 20|25|30|10|15" },
    { "retro8_mouse_hold", "Mouse Hold Button; y|select" },
    { "retro8_mouse_cursor", "Mouse Cursor; off|on" },
    { "retro8_mouse_function", "Mouse Function; free|centre" },
    { "retro8_keyboard_dpad", "Keyboard D-Pad; arrows|wasd|esdf|ijkl|off" },
    { "retro8_keyboard_key", "Keyboard X Button; space|enter|shift|tab|escape|off" },
    { NULL, NULL },
  };

  static const char* value(const char* key)
  {
    retro_variable variable = { key, nullptr };
    return env.retro_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value ? variable.value : "";
  }

  void readOptions()
  {
    const std::string source = value("retro8_mouse_source");
    options.source = source == "right" ? Source::RIGHT : source == "left" ? Source::LEFT : source == "dpad" ? Source::DPAD
                   : source == "off" ? Source::OFF : Source::AUTO;

    const int speed = std::atoi(value("retro8_mouse_speed"));
    options.speed = speed > 0 ? float(speed) : 128.0f;

    const int deadzone = std::atoi(value("retro8_mouse_deadzone"));
    options.deadzone = deadzone > 0 && deadzone < 100 ? deadzone / 100.0f : 0.2f;

    const std::string hold = value("retro8_mouse_hold");
    options.hold = hold == "select" ? RETRO_DEVICE_ID_JOYPAD_SELECT : RETRO_DEVICE_ID_JOYPAD_Y;

    options.cursor = std::string(value("retro8_mouse_cursor")) == "on";
    options.centre = std::string(value("retro8_mouse_function")) == "centre";
  }

  void reset()
  {
    cursor = Cursor();
  }

  bool enabled()
  {
    return machine && (machine->memory().base()[0x5f2d] & 1) && options.source != Source::OFF;
  }

  bool holding()
  {
    return cursor.holding;
  }

  /* the stick's push past the deadzone, squared so small pushes stay precise */
  static bool stick(unsigned index, float& vx, float& vy)
  {
    const float x = env.inputState(0, RETRO_DEVICE_ANALOG, index, RETRO_DEVICE_ID_ANALOG_X) / 32768.0f;
    const float y = env.inputState(0, RETRO_DEVICE_ANALOG, index, RETRO_DEVICE_ID_ANALOG_Y) / 32768.0f;
    const float length = std::sqrt(x * x + y * y);
    if (length <= options.deadzone)
      return false;

    const float push = std::min(1.0f, (length - options.deadzone) / (1.0f - options.deadzone));
    const float scale = push * push * options.speed / 60.0f / length;
    vx = x * scale;
    vy = y * scale;
    return true;
  }

  /* the stick's direction scaled by how far it is pushed past the deadzone, for Return to Centre */
  static bool aim(unsigned index, float& ax, float& ay)
  {
    const float x = env.inputState(0, RETRO_DEVICE_ANALOG, index, RETRO_DEVICE_ID_ANALOG_X) / 32768.0f;
    const float y = env.inputState(0, RETRO_DEVICE_ANALOG, index, RETRO_DEVICE_ID_ANALOG_Y) / 32768.0f;
    const float length = std::sqrt(x * x + y * y);
    if (length <= options.deadzone)
      return false;

    const float push = std::min(1.0f, (length - options.deadzone) / (1.0f - options.deadzone));
    ax = x / length * push;
    ay = y / length * push;
    return true;
  }

  static void announce()
  {
    if (cursor.announced)
      return;
    cursor.announced = true;

    const char* hold = options.hold == RETRO_DEVICE_ID_JOYPAD_SELECT ? "Select" : "Y";
    static char text[160];
    switch (options.source)
    {
    case Source::RIGHT: snprintf(text, sizeof(text), "Mouse: Right Stick moves, L1 clicks, R1 right clicks, L2 middle clicks"); break;
    case Source::LEFT: snprintf(text, sizeof(text), "Mouse: Left Stick moves, L1 clicks, R1 right clicks"); break;
    case Source::DPAD: snprintf(text, sizeof(text), "Mouse: hold %s and use the D-Pad, L1 clicks, R1 right clicks", hold); break;
    default: snprintf(text, sizeof(text), "Mouse: a stick or %s with the D-Pad moves, L1 clicks, R1 right clicks", hold); break;
    }

    retro_message message = { text, 240 };
    env.retro_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &message);
  }

  /* runs every 60 Hz frame before the buttons are read */
  void update()
  {
    if (!enabled())
    {
      cursor.holding = false;
      cursor.buttons = 0;
      return;
    }

    announce();

    const auto pad = [](unsigned id) { return env.inputState(0, RETRO_DEVICE_JOYPAD, 0, id) != 0; };

    float vx = 0.0f, vy = 0.0f;
    float ax = 0.0f, ay = 0.0f;
    bool aimed = false;
    const bool sticks = options.source == Source::AUTO || options.source == Source::RIGHT || options.source == Source::LEFT;
    const bool right = options.source == Source::RIGHT || options.source == Source::AUTO;
    const bool left = options.source == Source::LEFT || options.source == Source::AUTO;
    if (options.centre)
    {
      aimed = right && aim(RETRO_DEVICE_INDEX_ANALOG_RIGHT, ax, ay);
      if (!aimed && left)
        aimed = aim(RETRO_DEVICE_INDEX_ANALOG_LEFT, ax, ay);
    }
    else
    {
      if (right)
        stick(RETRO_DEVICE_INDEX_ANALOG_RIGHT, vx, vy);
      if (left && vx == 0.0f && vy == 0.0f)
        stick(RETRO_DEVICE_INDEX_ANALOG_LEFT, vx, vy);
    }

    uint8_t buttons = 0;
    cursor.holding = (options.source == Source::AUTO || options.source == Source::DPAD) && pad(options.hold);
    if (cursor.holding)
    {
      const int dx = pad(RETRO_DEVICE_ID_JOYPAD_RIGHT) - pad(RETRO_DEVICE_ID_JOYPAD_LEFT);
      const int dy = pad(RETRO_DEVICE_ID_JOYPAD_DOWN) - pad(RETRO_DEVICE_ID_JOYPAD_UP);
      if (dx || dy)
      {
        /* a single tap moves one pixel, holding speeds up to the stick's full speed over half a second */
        const float step = cursor.dpadFrames == 0 ? 1.0f : std::min(options.speed / 60.0f, 0.5f + cursor.dpadFrames * options.speed / 1800.0f);
        if (cursor.dpadFrames == 0 || cursor.dpadFrames > 8)
        {
          vx += dx * step;
          vy += dy * step;
        }
        ++cursor.dpadFrames;
      }
      else
        cursor.dpadFrames = 0;

      if (pad(RETRO_DEVICE_ID_JOYPAD_A)) buttons |= 1;
      if (pad(RETRO_DEVICE_ID_JOYPAD_B)) buttons |= 2;
    }
    else
      cursor.dpadFrames = 0;

    if (sticks || options.source == Source::DPAD)
    {
      if (pad(RETRO_DEVICE_ID_JOYPAD_L)) buttons |= 1;
      if (pad(RETRO_DEVICE_ID_JOYPAD_R)) buttons |= 2;
      if (pad(RETRO_DEVICE_ID_JOYPAD_L2)) buttons |= 4;
    }

    /* a real mouse moves in its own pixels, halved so a desktop mouse does not race across 128 */
    vx += env.inputState(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X) * 0.5f;
    vy += env.inputState(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y) * 0.5f;
    if (env.inputState(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT)) buttons |= 1;
    if (env.inputState(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT)) buttons |= 2;
    if (env.inputState(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_MIDDLE)) buttons |= 4;
    if (env.inputState(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELUP)) ++cursor.wheel;
    if (env.inputState(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELDOWN)) --cursor.wheel;

    /* a touch screen places the cursor where it is pressed, and only then, since an idle pointer reads as the centre */
    if (env.inputState(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_PRESSED))
    {
      const float px = (env.inputState(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X) + 32768) * 128.0f / 65536.0f;
      const float py = (env.inputState(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y) + 32768) * 128.0f / 65536.0f;
      vx += px - cursor.x;
      vy += py - cursor.y;
      buttons |= 1;
    }

    if (options.centre && aimed)
    {
      vx = 63.5f + ax * 63.5f - cursor.x;
      vy = 63.5f + ay * 63.5f - cursor.y;
    }
    else if (options.centre && vx == 0.0f && vy == 0.0f && !(buttons & 1))
    {
      /* nothing is moving it, so it eases back to the middle */
      vx = (63.5f - cursor.x) * 0.25f;
      vy = (63.5f - cursor.y) * 0.25f;
      if (std::fabs(vx) < 0.05f && std::fabs(vy) < 0.05f) vx = 63.5f - cursor.x, vy = 63.5f - cursor.y;
    }

    cursor.x = std::min(127.0f, std::max(0.0f, cursor.x + vx));
    cursor.y = std::min(127.0f, std::max(0.0f, cursor.y + vy));
    cursor.movedX += vx;
    cursor.movedY += vy;
    cursor.buttons = buttons;

    auto& state = machine->state().mouse;
    state.x = int32_t(cursor.x);
    state.y = int32_t(cursor.y);
    state.buttons = cursor.buttons;
    state.wheel = cursor.wheel;
    state.dx = int32_t(cursor.movedX);
    state.dy = int32_t(cursor.movedY);
  }

  /* called once the cart has run a frame, so movement and the wheel count again from there */
  void consumed()
  {
    cursor.movedX -= int32_t(cursor.movedX);
    cursor.movedY -= int32_t(cursor.movedY);
    cursor.wheel = 0;
    if (machine)
    {
      machine->state().mouse.wheel = 0;
      machine->state().mouse.dx = 0;
      machine->state().mouse.dy = 0;
    }
  }

  /* 0x5f2d bit 1 makes the left and right buttons press O and X */
  bool pressesButton(unsigned button)
  {
    if (!enabled() || !(machine->memory().base()[0x5f2d] & 2))
      return false;
    return (button == 4 && (cursor.buttons & 1)) || (button == 5 && (cursor.buttons & 2));
  }

  bool drawCursor()
  {
    return options.cursor && enabled();
  }
}

/* Carts in devkit mode read the keyboard with stat(28) for keys held and stat(30) and stat(31) for typed characters. The
   D-Pad, O, X and the spare X button stand in for keys, and a real keyboard from the frontend works as well. */
namespace keyboard
{
  struct Key { uint8_t code; char typed; };

  static const Key directions[][4] = {
    { { 82, 0 }, { 81, 0 }, { 80, 0 }, { 79, 0 } },          /* arrows: up, down, left, right */
    { { 26, 'w' }, { 22, 's' }, { 4, 'a' }, { 7, 'd' } },
    { { 8, 'e' }, { 7, 'd' }, { 22, 's' }, { 9, 'f' } },
    { { 12, 'i' }, { 14, 'k' }, { 13, 'j' }, { 15, 'l' } },
  };

  int dpad = 0;
  Key extra = { 44, ' ' };
  std::array<bool, 256> previous = {};

  void readOptions()
  {
    const std::string set = mouse::value("retro8_keyboard_dpad");
    dpad = set == "off" ? -1 : set == "wasd" ? 1 : set == "esdf" ? 2 : set == "ijkl" ? 3 : 0;

    const std::string key = mouse::value("retro8_keyboard_key");
    extra = key == "enter" ? Key{ 40, '\r' } : key == "shift" ? Key{ 225, 0 } : key == "tab" ? Key{ 43, '\t' }
          : key == "escape" ? Key{ 41, 0 } : key == "off" ? Key{ 0, 0 } : Key{ 44, ' ' };
  }

  /* a real keyboard, from the libretro key to the SDL scancode PICO-8 uses and the character it types */
  static void real(std::array<bool, 256>& held)
  {
    const auto down = [](unsigned key) { return env.inputState(0, RETRO_DEVICE_KEYBOARD, 0, key) != 0; };
    for (unsigned i = 0; i < 26; ++i)
      if (down(RETROK_a + i)) held[4 + i] = true;
    for (unsigned i = 1; i <= 9; ++i)
      if (down(RETROK_0 + i)) held[29 + i] = true;
    const unsigned keys[][2] = {
      { RETROK_0, 39 }, { RETROK_RETURN, 40 }, { RETROK_ESCAPE, 41 }, { RETROK_BACKSPACE, 42 }, { RETROK_TAB, 43 },
      { RETROK_SPACE, 44 }, { RETROK_RIGHT, 79 }, { RETROK_LEFT, 80 }, { RETROK_DOWN, 81 }, { RETROK_UP, 82 },
      { RETROK_LCTRL, 224 }, { RETROK_LSHIFT, 225 }, { RETROK_LALT, 226 }, { RETROK_RCTRL, 228 }, { RETROK_RSHIFT, 229 },
      { RETROK_RALT, 230 },
    };
    for (const auto& key : keys)
      if (down(key[0])) held[key[1]] = true;
  }

  static char typed(int code)
  {
    if (code >= 4 && code <= 29) return char('a' + code - 4);
    if (code >= 30 && code <= 38) return char('1' + code - 30);
    switch (code)
    {
    case 39: return '0';
    case 40: return '\r';
    case 42: return '\b';
    case 43: return '\t';
    case 44: return ' ';
    default: return 0;
    }
  }

  /* runs every 60 Hz frame after the mouse, so the D-Pad only types while it is not moving the cursor */
  void update()
  {
    if (!machine)
      return;

    auto& state = machine->state().keyboard;
    if (!(machine->memory().base()[0x5f2d] & 1))
    {
      state.held.fill(false);
      state.typed.clear();
      previous.fill(false);
      return;
    }

    std::array<bool, 256> held = {};
    const auto pad = [](unsigned id) { return env.inputState(0, RETRO_DEVICE_JOYPAD, 0, id) != 0; };
    if (!mouse::holding())
    {
      static const unsigned ids[] = {
        RETRO_DEVICE_ID_JOYPAD_UP, RETRO_DEVICE_ID_JOYPAD_DOWN, RETRO_DEVICE_ID_JOYPAD_LEFT, RETRO_DEVICE_ID_JOYPAD_RIGHT
      };
      for (int i = 0; dpad >= 0 && i < 4; ++i)
        if (pad(ids[i])) held[directions[dpad][i].code] = true;
      if (pad(RETRO_DEVICE_ID_JOYPAD_A)) held[29] = true;
      if (pad(RETRO_DEVICE_ID_JOYPAD_B)) held[27] = true;
    }
    if (extra.code && pad(RETRO_DEVICE_ID_JOYPAD_X)) held[extra.code] = true;
    real(held);

    for (int code = 0; code < 256; ++code)
      if (held[code] && !previous[code] && typed(code) && state.typed.size() < 32)
        state.typed += typed(code);

    previous = held;
    state.held = held;
  }
}

//TODO
uint32_t Platform::getTicks() { return 0; }

static bool tryScreen32() {
  retro_pixel_format pixelFormat = RETRO_PIXEL_FORMAT_XRGB8888;
  if (!env.retro_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &pixelFormat))
    return false;
  env.isRGB32 = true;
  screen32 = new Screen32();
  env.logger(retro_log_level::RETRO_LOG_INFO, "Initializing XRGB8888 screen buffer of %d bytes\n", 4*r8::gfx::SCREEN_WIDTH*r8::gfx::SCREEN_HEIGHT);
  return true;
}

static bool tryScreen16() {
  retro_pixel_format pixelFormat = RETRO_PIXEL_FORMAT_RGB565;
  if (!env.retro_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &pixelFormat))
    return false;
  env.isRGB32 = false;
  screen16 = new Screen16();
  env.logger(retro_log_level::RETRO_LOG_INFO, "Initializing RGB565 screen buffer of %d bytes\n", 2*r8::gfx::SCREEN_WIDTH*r8::gfx::SCREEN_HEIGHT);
  return true;
}

static struct retro_input_descriptor input_desc[] = {
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "Left" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "Down" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "Up" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "O" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "X" },
    { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "Left" },
    { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "Down" },
    { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },
    { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "Up" },
    { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "O" },
    { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "X" },
    { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "Left" },
    { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "Down" },
    { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },
    { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "Up" },
    { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "O" },
    { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "X" },
    { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "Left" },
    { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "Down" },
    { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },
    { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "Up" },
    { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "O" },
    { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "X" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Mouse D-Pad Hold" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "Mouse Left Click" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "Mouse Right Click" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "Mouse Middle Click" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Keyboard Key" },
    { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Mouse X" },
    { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Mouse Y" },

    { 0 },
};


extern "C"
{
  unsigned retro_api_version()
  {
    return RETRO_API_VERSION;
  }

  void retro_init()
  {
    audioBuffer = new int16_t[SAMPLE_RATE * 2];
    env.logger(retro_log_level::RETRO_LOG_INFO, "Initializing audio buffer of %d bytes\n", sizeof(int16_t) * SAMPLE_RATE * 2);
  }

  void retro_deinit()
  {
    delete[] audioBuffer;
    //TODO: release all structures bound to Lua etc
  }

  void retro_get_system_info(retro_system_info* info)
  {
    std::memset(info, 0, sizeof(*info));

    info->library_name = "retro-8 (alpha)";
#ifndef GIT_VERSION
#define GIT_VERSION ""
#endif
    info->library_version = "0.2" GIT_VERSION;
    info->need_fullpath = false;
    info->valid_extensions = "p8|png";
  }

  void retro_get_system_av_info(retro_system_av_info* info)
  {
    info->timing.fps = 60.0f;
    info->timing.sample_rate = SAMPLE_RATE;
    info->geometry.base_width = retro8::gfx::SCREEN_WIDTH;
    info->geometry.base_height = retro8::gfx::SCREEN_HEIGHT;
    info->geometry.max_width = retro8::gfx::SCREEN_WIDTH;
    info->geometry.max_height = retro8::gfx::SCREEN_HEIGHT;
    info->geometry.aspect_ratio = retro8::gfx::SCREEN_WIDTH / float(retro8::gfx::SCREEN_HEIGHT);
  }

  void retro_set_environment(retro_environment_t e)
  {
    env.retro_cb = e;

    retro_log_callback logger;
    if (e(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logger))
      env.logger = logger.log;

    e(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, input_desc);

    unsigned version = 0;
    if (e(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version) && version >= 1)
      e(RETRO_ENVIRONMENT_SET_CORE_OPTIONS, (void*)mouse::definitions);
    else
      e(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)mouse::legacy);
  }

  void retro_set_video_refresh(retro_video_refresh_t callback) { env.video = callback; }
  void retro_set_audio_sample(retro_audio_sample_t callback) { env.audio = callback; }
  void retro_set_audio_sample_batch(retro_audio_sample_batch_t callback) { env.audioBatch = callback; }
  void retro_set_input_poll(retro_input_poll_t callback) { env.inputPoll = callback; }
  void retro_set_input_state(retro_input_state_t callback) { env.inputState = callback; }
  void retro_set_controller_port_device(unsigned port, unsigned device) { /* TODO */ }
  

  /* a state is the machine's own blob behind its length, so the zero padding the frontend's buffer adds is ignored */
  size_t retro_serialize_size(void)
  {
    std::string state;
    if (!machine || !machine->saveState(state))
      return 0;

    /* the Lua heap grows and shrinks between frames, so leave room for it to change before the save */
    const size_t size = sizeof(uint32_t) + state.size();
    return size + size / 4 + 65536;
  }

  bool retro_serialize(void *data, size_t size)
  {
    std::string state;
    if (!machine || !machine->saveState(state))
      return false;

    const uint32_t length = uint32_t(state.size());
    if (size < sizeof(length) + state.size())
      return false;

    std::memcpy(data, &length, sizeof(length));
    std::memcpy(static_cast<uint8_t*>(data) + sizeof(length), state.data(), state.size());
    std::memset(static_cast<uint8_t*>(data) + sizeof(length) + state.size(), 0, size - sizeof(length) - state.size());
    return true;
  }

  bool retro_unserialize(const void *data, size_t size)
  {
    uint32_t length = 0;
    if (!machine || size < sizeof(length))
      return false;

    std::memcpy(&length, data, sizeof(length));
    if (size - sizeof(length) < length)
      return false;

    return machine->loadState(static_cast<const uint8_t*>(data) + sizeof(length), length);
  }
  void retro_cheat_reset(void) { }
  void retro_cheat_set(unsigned index, bool enabled, const char *code) { }
  unsigned retro_get_region(void) { return 0; }
  void *retro_get_memory_data(unsigned id) { return nullptr; }
  size_t retro_get_memory_size(unsigned id) { return 0; }

  bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info) { return false; }
  bool retro_load_game(const retro_game_info* info)
  {
    machine = new r8::Machine();
    machine->font().load();
    machine->code().loadAPI();
    input.setMachine(machine);
    mouse::reset();
    mouse::readOptions();
    keyboard::readOptions();

    const char* saveDirectory = nullptr;
    lua::cartdataDirectory.clear();
    if (env.retro_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &saveDirectory) && saveDirectory && *saveDirectory)
    {
      lua::cartdataDirectory = std::string(saveDirectory) + "/retro8";
#ifdef _WIN32
      _mkdir(lua::cartdataDirectory.c_str());
#else
      mkdir(lua::cartdataDirectory.c_str(), 0755);
#endif
    }

    if (info && info->data)
    {
      input.reset();
      std::memset(env.buttonStates, 0, sizeof(env.buttonStates));

      const char* bdata = static_cast<const char*>(info->data);

      /* other carts that load(), reload() and #include name come from the same folder */
      r8::io::cartDirectory.clear();
      lua::cartParam.clear();
      lua::cartBreadcrumb.clear();
      lua::pendingCart.clear();
      if (info->path)
      {
        const std::string path(info->path);
        const size_t slash = path.find_last_of('/');
        if (slash != std::string::npos)
          r8::io::cartDirectory = path.substr(0, slash);
      }

      env.logger(RETRO_LOG_INFO, "[retro8] Loading %s\n", info->path);


      if (std::memcmp(bdata, "\x89PNG", 4) == 0)
      {
        env.logger(RETRO_LOG_INFO, "[retro8] Game is in PNG format, decoding it.\n");

        std::vector<uint8_t> out;
        unsigned long width, height;
        int result = Platform::loadPNG(out, width, height, (uint8_t*)bdata, info->size, true);
        if (result != 0)
        {
          env.logger(RETRO_LOG_ERROR, "[retro8] The cartridge image could not be decoded\n");
          return false;
        }
        r8::io::Stegano stegano;
        uint32_t *buf = new uint32_t[out.size() / 4];
        for (int i = 0; i < out.size() / 4; i++) {
          buf[i] = out[4 * i] | (out[4 * i + 1] << 8) | (out[4 * i + 2] << 16) | (out[4 * i + 3] << 24);
        }
        r8::io::PngData pngData = { buf, NULL, out.size() / 4 };
        const bool loaded = stegano.load(pngData, *machine);
        delete[] buf;
        if (!loaded)
        {
          env.logger(RETRO_LOG_ERROR, "[retro8] The cartridge code is damaged or could not be compiled\n");
          return false;
        }
      }
      else
      {
        //TODO: not efficient since it's copied and it's not checking for '\0'
        std::string raw(bdata, info->size);
        if (!loader.loadRaw(raw, *machine))
        {
          env.logger(RETRO_LOG_ERROR, "[retro8] The cartridge code could not be compiled\n");
          return false;
        }
      }


      if (machine->code().hasInit())
      {
        //_initFuture = std::async(std::launch::async, []() {
        LIBRETRO_LOG("[retro8] Cartridge has _init() function, calling it.");
          machine->code().init();
          LIBRETRO_LOG("[retro8] _init() function completed execution.");
        //});
      }

#if SOUND_ENABLED
      machine->sound().init();
#endif

      env.frameCounter = 0;

      screen16 = NULL;
      screen32 = NULL;
#ifdef USE_RGB565
      if (!tryScreen16() && !tryScreen32())
#else
      if (!tryScreen32() && !tryScreen16())
#endif
	{
	  env.logger(retro_log_level::RETRO_LOG_ERROR, "Couldn't find compatible pixel format\n");
	  return false;
	}

      return true;
    }

    return false;
  }

  void retro_unload_game(void)
  {
    if (machine)
      lua::cartdataFlush();

    /* TODO */
    if (screen16)
      delete screen16;
    if (screen32)
      delete screen32;
    screen16 = NULL;
    screen32 = NULL;
    delete machine;
  }

  void retro_run()
  {
    env.inputPoll();

    bool updated = false;
    if (env.retro_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
    {
      mouse::readOptions();
      keyboard::readOptions();
    }
    mouse::update();
    keyboard::update();

    const static int16_t mapping[retro8::BUTTON_COUNT] = {
      RETRO_DEVICE_ID_JOYPAD_LEFT,
      RETRO_DEVICE_ID_JOYPAD_RIGHT,
      RETRO_DEVICE_ID_JOYPAD_UP,
      RETRO_DEVICE_ID_JOYPAD_DOWN,
      RETRO_DEVICE_ID_JOYPAD_A,
      RETRO_DEVICE_ID_JOYPAD_B,
    };
    for (unsigned player = 0; player < retro8::PLAYER_COUNT; ++player)
      for (unsigned button = 0; button < retro8::BUTTON_COUNT; ++button)
      {
        /* while the hold button turns the d-pad into the cursor, the d-pad and O and X stop reaching the cart */
        bool pressed = env.inputState(player, RETRO_DEVICE_JOYPAD, 0, mapping[button]) && !(player == 0 && mouse::holding());
        if (player == 0 && mouse::pressesButton(button))
          pressed = true;
        if (env.buttonStates[player][button] != pressed)
          input.manageKey(player, button, pressed);
        env.buttonStates[player][button] = pressed;
      }

    /* if code is at 60fps or every 2 frames (30fps) */
    if (machine->code().require60fps() || env.frameCounter % 2 == 0)
    {
      input.manageKeyRepeat();
      /* call _update and _draw of PICO-8 code, or carry on from the last flip() */
      machine->code().frame();
      mouse::consumed();
      lua::cartdataFlush();

      if (!lua::pendingCart.empty() && r8::io::switchCart(machine))
        input.setMachine(machine);

      /* rasterize screen memory to ARGB framebuffer */
      auto* data = machine->memory().displayData();

      const uint8_t mode = machine->memory().base()[0x5f2c];
      if (env.isRGB32)
	screen32->draw(data, machine->memory(), mode);
      else
	screen16->draw(data, machine->memory(), mode);
    }

    const bool arrow = mouse::drawCursor();
    const int ax = int(mouse::cursor.x), ay = int(mouse::cursor.y);
    if (env.isRGB32)
      env.video(arrow ? screen32->getBufferWithCursor(ax, ay) : screen32->getBuffer(), r8::gfx::SCREEN_WIDTH, r8::gfx::SCREEN_HEIGHT, r8::gfx::SCREEN_WIDTH * sizeof(uint32_t));
    else
      env.video(arrow ? screen16->getBufferWithCursor(ax, ay) : screen16->getBuffer(), r8::gfx::SCREEN_WIDTH, r8::gfx::SCREEN_HEIGHT, r8::gfx::SCREEN_WIDTH * sizeof(uint16_t));
    ++env.frameCounter;

#if SOUND_ENABLED
    machine->sound().renderSounds(audioBuffer, SAMPLES_PER_FRAME);
    
    /* duplicate channels */
    auto* audioBuffer2 = audioBuffer + SAMPLE_RATE;
    for (size_t i = 0; i < SAMPLES_PER_FRAME; ++i)
    {
      audioBuffer2[2*i] = audioBuffer[i];
      audioBuffer2[2*i + 1] = audioBuffer[i];
    }
    
    env.audioBatch(audioBuffer2, SAMPLES_PER_FRAME);
#else
    memset(audioBuffer, 0, sizeof(audioBuffer[0]) * 2 * SAMPLES_PER_FRAME);
    env.audioBatch(audioBuffer, SAMPLES_PER_FRAME);
#endif

    input.tick();

  }

  void retro_reset()
  {

  }
}
