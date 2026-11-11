#pragma once

#include <string>

struct lua_State;

namespace lua
{
  void registerFunctions(lua_State* state);

  /* where cartdata() keeps each cart's save, empty to keep nothing */
  extern std::string cartdataDirectory;
  void cartdataFlush();

  /* load() leaves the cart it asked for here, and the host switches to it once the frame has ended */
  extern std::string pendingCart;
  extern std::string pendingParam;
  /* the parameter string and breadcrumb the running cart was loaded with, for stat(6) and stat(100) */
  extern std::string cartParam;
  extern std::string cartBreadcrumb;

  class Code
  {
  public:
    struct Result
    {
      bool success;
      std::string error;
    };  
  
  private:
    enum Phase { PHASE_TOP, PHASE_INIT, PHASE_UPDATE, PHASE_DRAW };

    lua_State* L;
    lua_State* _thread;
    int _threadRef;
    bool _suspended;
    Phase _phase;

    const void* _init;
    const void* _update;
    const void* _update60;
    const void* _draw;

    double _time;
    double _frameStart;
    int _waitFrames;

  public:
    int _fps;

    Code() : L(nullptr), _thread(nullptr), _threadRef(0), _suspended(false), _phase(PHASE_TOP),
      _init(nullptr), _update(nullptr), _update60(nullptr), _draw(nullptr), _time(0.0), _frameStart(0.0), _waitFrames(0), _fps(0) { }
    ~Code();

    void loadAPI();


    void printError(const char* where);
    void lookupCallbacks();
    void start(Phase phase);
    void step();
    void dropThread();
    bool suspended() const { return _suspended; }
    double time() const { return _time; }
    void waitFrames(int frames) { _waitFrames = frames; }
    double cpuLoad() const;
    lua_State* cartThread() const { return _thread; }
    lua_State* mainState() const { return L; }
    bool isCartThread(lua_State* state) const { return state == _thread; }
    void frame();
    bool initFromSource(const std::string& code);
    void callFunction(const char* name, int ret = 0);

    bool hasUpdate() const { return _update != nullptr || _update60 != nullptr; }
    bool hasDraw() const { return _draw != nullptr; }
    bool require60fps() const { return _fps ? _fps >= 60 : _update60 != nullptr; }
    void setFps(int fps) { _fps = fps >= 60 ? 60 : 30; }
    bool hasInit() const { return _init != nullptr; }

    void init();
    void update();
    void draw();

    /* the values present before the cart runs, which save states refer to by name rather than storing */
    void buildPermanents();
    /* the cart's globals, its suspended thread and the frame timing, as written by Eris */
    bool saveState(std::string& out);
    bool loadState(const std::string& in);

#if TEST_MODE
    lua_State* state() const { return L; }
#endif
  };
}