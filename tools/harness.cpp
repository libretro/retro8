#include "common.h"
#include "io/stegano.h"
#include "io/loader.h"
#include "vm/machine.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <cstring>
#include <csignal>
#include <chrono>
#include <unistd.h>
#include "lua/lua.hpp"
#include "vm/input.h"
#include "io/cart.h"
namespace r8 = retro8;
r8::Machine *machine;
uint32_t Platform::getTicks() { return 0; }

static volatile sig_atomic_t trace_now = 0;

static void trace_hook(lua_State *L, lua_Debug *) {
    if (!trace_now) return;
    luaL_traceback(L, L, "stuck here", 0);
    printf("%s\n", lua_tostring(L, -1));
    _exit(4);
}

#include <map>
#include <string>
#include <vector>
#include <algorithm>
static std::map<std::string, long> profile_hits;

static void profile_hook(lua_State *L, lua_Debug *ar) {
    lua_Debug info;
    if (!lua_getstack(L, 0, &info) || !lua_getinfo(L, "Sl", &info)) return;
    char key[96];
    snprintf(key, sizeof(key), "line %d (function at %d)", info.currentline, info.linedefined);
    profile_hits[key]++;
}

static void profile_report() {
    std::vector<std::pair<long, std::string>> rows;
    long total = 0;
    for (auto &hit : profile_hits) { rows.push_back({hit.second, hit.first}); total += hit.second; }
    std::sort(rows.rbegin(), rows.rend());
    for (size_t i = 0; i < rows.size() && i < 15; ++i)
        printf("PROFILE %5.1f%% %s\n", 100.0 * rows[i].first / (total ? total : 1), rows[i].second.c_str());
}

static const uint8_t dump_rgb[32][3] = {
    {0, 0, 0}, {29, 43, 83}, {126, 37, 83}, {0, 135, 81}, {171, 82, 54}, {95, 87, 79}, {194, 195, 199}, {255, 241, 232},
    {255, 0, 77}, {255, 163, 0}, {255, 236, 39}, {0, 228, 54}, {41, 173, 255}, {131, 118, 156}, {255, 119, 168}, {255, 204, 170},
    {41, 24, 20}, {17, 29, 53}, {66, 33, 54}, {18, 83, 89}, {116, 47, 41}, {73, 51, 59}, {162, 136, 121}, {243, 239, 125},
    {190, 18, 80}, {255, 108, 36}, {168, 231, 46}, {0, 181, 67}, {6, 90, 181}, {117, 70, 101}, {255, 110, 89}, {255, 157, 129}};

static void dump_frame(const char *dir, int frame) {
    char path[512];
    snprintf(path, sizeof(path), "%s/f%05d.ppm", dir, frame);
    FILE *out = fopen(path, "wb");
    if (!out) return;
    fprintf(out, "P6\n128 128\n255\n");
    const uint8_t *mem = (const uint8_t *) machine->memory().base();
    for (int i = 0; i < 128 * 128; i++) {
        uint8_t pair = mem[0x6000 + i / 2];
        uint8_t c = (i & 1) ? pair >> 4 : pair & 15;
        uint8_t s = machine->memory().screenEntry(retro8::color_t(c), i / 128);
        const uint8_t *rgb = dump_rgb[(s & 15) | ((s & 0x80) ? 16 : 0)];
        fwrite(rgb, 1, 3, out);
    }
    fclose(out);
}

static void on_alarm(int) {
    trace_now = 1;
}

int main(int argc, char **argv) {
    if (getenv("P8_TRACE_AFTER")) {
        signal(SIGALRM, on_alarm);
        alarm(atoi(getenv("P8_TRACE_AFTER")));
    }
    std::ifstream f(argv[1], std::ios::binary);
    {
        const std::string path(argv[1]);
        const size_t slash = path.find_last_of('/');
        r8::io::cartDirectory = slash == std::string::npos ? "." : path.substr(0, slash);
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    machine = new r8::Machine();
    machine->font().load();
    machine->code().loadAPI();
    if (getenv("P8_SAVE_DIR")) lua::cartdataDirectory = getenv("P8_SAVE_DIR");
    if (getenv("P8_TRACE_AFTER")) lua_sethook(machine->code().mainState(), trace_hook, LUA_MASKCOUNT, 1000);
    int frames = argc > 2 ? atoi(argv[2]) : 0;
    if (data.size() > 4 && std::memcmp(data.data(), "\x89PNG", 4) == 0) {
        std::vector<uint8_t> out;
        unsigned long w, h;
        if (Platform::loadPNG(out, w, h, data.data(), data.size(), true) != 0) { printf("PNGFAIL\n"); return 2; }
        uint32_t *buf = new uint32_t[out.size() / 4];
        for (size_t i = 0; i < out.size() / 4; i++)
            buf[i] = out[4 * i] | (out[4 * i + 1] << 8) | (out[4 * i + 2] << 16) | (out[4 * i + 3] << 24);
        r8::io::PngData png = {buf, NULL, out.size() / 4};
        r8::io::Stegano s;
        if (!s.load(png, *machine)) { printf("Error on load\nunreadable or uncompilable cart\n"); return 3; }
    } else {
        std::string raw((const char *) data.data(), data.size());
        r8::io::Loader l;
        if (!l.loadRaw(raw, *machine)) { printf("Error on load\nuncompilable cart\n"); return 3; }
    }
    if (frames > 0) {
        if (getenv("P8_PROFILE") && machine->code().cartThread())
            lua_sethook(machine->code().cartThread(), profile_hook, LUA_MASKCOUNT, 1000);
        if (machine->code().hasInit()) machine->code().init();
        const auto started = std::chrono::steady_clock::now();
        static r8::input::InputManager input;
        input.setMachine(machine);
        const char *dump = getenv("P8_DUMP");
        int every = getenv("P8_DUMP_EVERY") ? atoi(getenv("P8_DUMP_EVERY")) : 30;
        const char *press = getenv("P8_PRESS");
        FILE *wav = getenv("P8_WAV") ? fopen(getenv("P8_WAV"), "wb") : NULL;
        uint32_t wavBytes = 0;
        if (wav) fwrite("RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x01\0\x44\xac\0\0\x88\x58\x01\0\x02\0\x10\0data\0\0\0\0", 1, 44, wav);
        for (int i = 0; i < frames; i++) {
            if (press) {
                const char *p = press;
                while (*p) {
                    int at = 0, button = 0, len = 0;
                    int player = 0;
                    if (sscanf(p, "%d:%d:%d:%d", &at, &button, &len, &player) >= 3 && player >= 0 && player < (int) retro8::PLAYER_COUNT) {
                        if (i == at) input.manageKey(player, button, true);
                        if (i == at + len) input.manageKey(player, button, false);
                    }
                    p = strchr(p, ',');
                    if (!p) break;
                    ++p;
                }
            }
            /* P8_KEYS="frame:scancode:frames,..." holds a devkit keyboard key, typing its character if it has one */
            if (const char *spec = getenv("P8_KEYS")) {
                auto &keyboard = machine->state().keyboard;
                const char *p = spec;
                while (*p) {
                    int at = 0, code = 0, len = 0;
                    if (sscanf(p, "%d:%d:%d", &at, &code, &len) == 3 && code >= 0 && code < 256) {
                        if (i == at) {
                            keyboard.held[code] = true;
                            if (code >= 4 && code <= 29) keyboard.typed += char('a' + code - 4);
                            if (code == 44) keyboard.typed += ' ';
                        }
                        if (i == at + len) keyboard.held[code] = false;
                    }
                    p = strchr(p, ',');
                    if (!p) break;
                    ++p;
                }
            }
            /* P8_MOUSE="frame:x:y:buttons,..." sets the devkit mouse from that frame on */
            if (const char *spec = getenv("P8_MOUSE")) {
                const char *p = spec;
                while (*p) {
                    int at = 0, x = 0, y = 0, buttons = 0;
                    if (sscanf(p, "%d:%d:%d:%d", &at, &x, &y, &buttons) == 4 && i == at) {
                        auto &mouse = machine->state().mouse;
                        mouse.dx = x - mouse.x;
                        mouse.dy = y - mouse.y;
                        mouse.x = x;
                        mouse.y = y;
                        mouse.buttons = buttons;
                    }
                    p = strchr(p, ',');
                    if (!p) break;
                    ++p;
                }
            }
            machine->code().frame();
            if (const char *spec = getenv("P8_STATE")) {
                /* P8_STATE=S:L saves after frame S and restores that state after frame L */
                static std::string saved;
                int saveAt = -1, loadAt = -1;
                sscanf(spec, "%d:%d", &saveAt, &loadAt);
                if (i == saveAt) {
                    const bool ok = machine->saveState(saved);
                    printf("STATE save %s %zu\n", ok ? "ok" : "failed", saved.size());
                    if (const char *file = getenv("P8_STATE_OUT")) {
                        FILE *f = fopen(file, "wb");
                        if (f) { fwrite(saved.data(), 1, saved.size(), f); fclose(f); }
                    }
                }
                /* P8_STATE_IN loads a saved state from a file after frame 1, or after the load frame of P8_STATE when one is given */
                if (getenv("P8_STATE_IN") && i == (loadAt < 0 ? 1 : loadAt)) {
                    std::ifstream in(getenv("P8_STATE_IN"), std::ios::binary);
                    saved.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
                    if (loadAt < 0) loadAt = 1;
                }
                if (i == loadAt) printf("STATE load %s\n", machine->loadState((const uint8_t *) saved.data(), saved.size()) ? "ok" : "failed");
            }
            if (!lua::pendingCart.empty()) {
                printf("LOAD %s [%s]\n", lua::pendingCart.c_str(), lua::pendingParam.c_str());
                if (r8::io::switchCart(machine)) input.setMachine(machine);
            }
            input.manageKeyRepeat();
            input.tick();
            if (dump && (i % every == 0 || i == frames - 1)) dump_frame(dump, i);
            if (wav) {
                static int16_t pcm[44100 / 30];
                const size_t count = machine->code().require60fps() ? 44100 / 60 : 44100 / 30;
                machine->sound().renderSounds(pcm, count);
                fwrite(pcm, sizeof(int16_t), count, wav);
                wavBytes += count * sizeof(int16_t);
            }
            lua::cartdataFlush();
        }
        if (wav) {
            const uint32_t riff = wavBytes + 36;
            fseek(wav, 4, SEEK_SET);
            fwrite(&riff, 4, 1, wav);
            fseek(wav, 40, SEEK_SET);
            fwrite(&wavBytes, 4, 1, wav);
            fclose(wav);
        }
        if (getenv("P8_TIME")) {
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            printf("FRAME_MS %.3f\n", ms / frames);
        }
    }
    if (getenv("P8_PROFILE")) profile_report();
    printf("DONE\n");
    return 0;
}
