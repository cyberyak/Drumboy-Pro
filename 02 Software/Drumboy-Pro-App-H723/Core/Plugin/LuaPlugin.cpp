#include "Plugin.h"

#if defined(DRUMBOY_PLUGINS) && defined(DRUMBOY_LUA)

#include <strings.h>

#include "Lcd.h"
#include "LuaHeap.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

// clang-format off

/* Settings ---------------------------------------------------------*/

const uint8_t  kLuaPluginMaxCount                   = 8;
const char     kLuaPluginFolder[]                   = "Plugins";
const uint8_t  kLuaPluginIdSize                     = 24;
const size_t   kLuaPluginMemoryLimit                = 48 * 1024;        // Per script, of the 96 KB Lua heap
const uint32_t kLuaLoadBudgetMs                     = 300;              // Loading + on_load()
const uint32_t kLuaEventBudgetMs                    = 2;                // Each on_<event>() call
const uint32_t kLuaFrameBudgetMs                    = 10;               // Each on_frame() call
const int      kLuaHookInstructions                 = 1000;             // Budget check interval
const uint16_t kLuaTextMaxLength                    = 120;

// clang-format on

/* Shared Call State (Lua runs only in the main loop, one call at a time) */

static FIL* scriptFile = nullptr;                                       // Controller's FIL, lent at boot
static uint32_t callStartCycles = 0;
static uint32_t callBudgetCycles = 0;
static Lcd* frameLcd = nullptr;                                         // Set only during on_frame()
static RGB16Color frameColor = WHITE;

static void budgetHook(lua_State* L, lua_Debug* ar) {
    (void)ar;
    if ((DWT_CYCCNT - callStartCycles) > callBudgetCycles) {
        luaL_error(L, "too slow (over %d ms)", (int)(callBudgetCycles / (SystemCoreClock / 1000)));
    }
}

static void startBudget(uint32_t ms) {
    callBudgetCycles = ms * (SystemCoreClock / 1000);
    callStartCycles = DWT_CYCCNT;
}

/* Lua Plugin -------------------------------------------------------*/

class LuaPlugin : public Plugin {
   public:
    void setup(const char* fileName) {
        // id = file name without ".lua"
        size_t len = strlen(fileName) - 4;
        if (len >= kLuaPluginIdSize) len = kLuaPluginIdSize - 1;
        memcpy(idText, fileName, len);
        idText[len] = '\0';
        snprintf(path, sizeof(path), "%s/%s", kLuaPluginFolder, fileName);
    }

    const char* id() const override { return idText; }
    const char* name() const override { return idText; }
    bool isActive() const override { return L != nullptr; }

    bool onStart() override;
    void onEvent(const PluginEvent& e) override;
    void onFrame(Lcd& lcd) override;

    size_t used = 0;

   private:
    lua_State* L = nullptr;
    char idText[kLuaPluginIdSize] = "";
    char path[48] = "";

    bool call(const char* function, int nargs, uint32_t budgetMs);
    void fail();
};

static LuaPlugin luaPlugins[kLuaPluginMaxCount];
static uint8_t luaPluginCount = 0;

/* Allocator --------------------------------------------------------*/

static void* luaAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    /// @brief lua_Alloc over the shared DTCM heap with a per-script memory limit.

    LuaPlugin* plugin = static_cast<LuaPlugin*>(ud);
    (void)osize;

    // 1. FREE
    if (nsize == 0) {
        if (ptr != nullptr) {
            plugin->used -= luaHeap_blockSize(ptr);
            luaHeap_free(ptr);
        }
        return nullptr;
    }

    // 2. SHRINK OR FITTING GROW IN PLACE
    size_t oldBlock = (ptr != nullptr) ? luaHeap_blockSize(ptr) : 0;
    if ((ptr != nullptr) && (nsize <= oldBlock)) return ptr;

    // 3. LIMIT
    if (plugin->used - oldBlock + nsize > kLuaPluginMemoryLimit) return nullptr;

    // 4. ALLOCATE + MOVE
    void* block = luaHeap_alloc(nsize);
    if (block == nullptr) return nullptr;
    plugin->used += luaHeap_blockSize(block);
    if (ptr != nullptr) {
        memcpy(block, ptr, (osize < nsize) ? osize : nsize);
        plugin->used -= oldBlock;
        luaHeap_free(ptr);
    }
    return block;
}

/* Script Loading ---------------------------------------------------*/

static const char* fileReader(lua_State* L, void* ud, size_t* size) {
    /// @brief lua_Reader streaming the script from the SD card in 512-byte chunks (buffer in DMA-reachable RAM_D1).

    (void)L;
    (void)ud;
    static char chunk[512] __attribute__((aligned(32)));
    UINT bytesRead = 0;
    if (f_read(scriptFile, chunk, sizeof(chunk), &bytesRead) != FR_OK) bytesRead = 0;
    *size = bytesRead;
    return (bytesRead > 0) ? chunk : nullptr;
}

/* Lua API: print / sys ---------------------------------------------*/

static int api_print(lua_State* L) {
    /// @brief print(...) shows the text in the plugin message line for a few seconds.

    char text[kLuaTextMaxLength + 1];
    size_t pos = 0;
    int n = lua_gettop(L);
    for (int i = 1; (i <= n) && (pos < kLuaTextMaxLength); i++) {
        size_t len;
        const char* s = luaL_tolstring(L, i, &len);
        if (i > 1) text[pos++] = ' ';
        if (len > kLuaTextMaxLength - pos) len = kLuaTextMaxLength - pos;
        memcpy(&text[pos], s, len);
        pos += len;
        lua_pop(L, 1);
    }
    text[pos] = '\0';

    lua_getfield(L, LUA_REGISTRYINDEX, "drumboy.id");
    char message[kLuaTextMaxLength + 32];
    snprintf(message, sizeof(message), "%s: %s", lua_tostring(L, -1), text);
    lua_pop(L, 1);
    pluginHost_message(message, false);
    return 0;
}

static int api_millis(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)HAL_GetTick());
    return 1;
}

static int api_memory(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "drumboy.plugin");
    LuaPlugin* plugin = static_cast<LuaPlugin*>(lua_touserdata(L, -1));
    lua_pushinteger(L, (lua_Integer)((plugin != nullptr) ? plugin->used : 0));
    return 1;
}

/* Lua API: screen (only inside on_frame) ---------------------------*/

static Lcd& frame(lua_State* L) {
    if (frameLcd == nullptr) luaL_error(L, "screen.* can only be used in on_frame()");
    frameLcd->setForeColor(frameColor);
    frameLcd->setBackColor(BLACK);
    return *frameLcd;
}

static uint16_t argX(lua_State* L, int i, Lcd& lcd) {
    lua_Integer v = luaL_checkinteger(L, i);
    return (uint16_t)((v < 0) ? 0 : (v >= lcd.getWidth()) ? lcd.getWidth() - 1 : v);
}

static uint16_t argY(lua_State* L, int i, Lcd& lcd) {
    lua_Integer v = luaL_checkinteger(L, i);
    return (uint16_t)((v < 0) ? 0 : (v >= lcd.getHeight()) ? lcd.getHeight() - 1 : v);
}

static uint16_t argSize(lua_State* L, int i, uint16_t max) {
    lua_Integer v = luaL_checkinteger(L, i);
    return (uint16_t)((v < 0) ? 0 : (v > max) ? max : v);
}

static int api_color(lua_State* L) {
    int r = (int)luaL_checkinteger(L, 1) & 0xFF;
    int g = (int)luaL_checkinteger(L, 2) & 0xFF;
    int b = (int)luaL_checkinteger(L, 3) & 0xFF;
    frameColor = (RGB16Color)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return 0;
}

static int api_font(lua_State* L) {
    static const LcdFont fonts[] = {FONT_05x07, FONT_07x09, FONT_10x14, FONT_14x18};
    lua_Integer n = luaL_checkinteger(L, 1);
    luaL_argcheck(L, (n >= 1) && (n <= 4), 1, "font must be 1-4");
    frame(L).setFont(fonts[n - 1]);
    return 0;
}

static int api_text(lua_State* L) {
    Lcd& lcd = frame(L);
    uint16_t x = argX(L, 1, lcd);
    uint16_t y = argY(L, 2, lcd);
    size_t len;
    const char* s = luaL_checklstring(L, 3, &len);
    const char* align = luaL_optstring(L, 4, "left");
    lcd.setAlignment((strcmp(align, "center") == 0) ? CENTER : (strcmp(align, "right") == 0) ? RIGHT : LEFT);
    if (len > kLuaTextMaxLength) len = kLuaTextMaxLength;
    lcd.drawText(s, (uint16_t)len, x, y);
    return 0;
}

static int api_rect(lua_State* L) {
    Lcd& lcd = frame(L);
    uint16_t x = argX(L, 1, lcd);
    uint16_t y = argY(L, 2, lcd);
    uint16_t w = argSize(L, 3, lcd.getWidth() - x);
    uint16_t h = argSize(L, 4, lcd.getHeight() - y);
    if ((w == 0) || (h == 0)) return 0;
    lua_toboolean(L, 5) ? lcd.fillRect(x, y, w, h) : lcd.drawRect(x, y, w, h);
    return 0;
}

static int api_clear(lua_State* L) {
    Lcd& lcd = frame(L);
    uint16_t x = argX(L, 1, lcd);
    uint16_t y = argY(L, 2, lcd);
    uint16_t w = argSize(L, 3, lcd.getWidth() - x);
    uint16_t h = argSize(L, 4, lcd.getHeight() - y);
    if ((w > 0) && (h > 0)) lcd.clearRect(x, y, w, h);
    return 0;
}

static int api_line(lua_State* L) {
    Lcd& lcd = frame(L);
    lcd.drawLine(argX(L, 1, lcd), argY(L, 2, lcd), argX(L, 3, lcd), argY(L, 4, lcd));
    return 0;
}

static int api_pixel(lua_State* L) {
    Lcd& lcd = frame(L);
    lcd.drawPixel(argX(L, 1, lcd), argY(L, 2, lcd));
    return 0;
}

static int api_circle(lua_State* L) {
    Lcd& lcd = frame(L);
    uint16_t x = argX(L, 1, lcd);
    uint16_t y = argY(L, 2, lcd);
    uint16_t r = argSize(L, 3, 200);
    if ((x < r) || (y < r) || (x + r >= lcd.getWidth()) || (y + r >= lcd.getHeight())) return 0;  // Fully on screen only
    lua_toboolean(L, 4) ? lcd.fillCircle(x, y, r) : lcd.drawCircle(x, y, r);
    return 0;
}

static const luaL_Reg screenApi[] = {
    {"color", api_color}, {"font", api_font}, {"text", api_text}, {"rect", api_rect}, {"clear", api_clear},
    {"line", api_line}, {"pixel", api_pixel}, {"circle", api_circle}, {nullptr, nullptr}};

static const luaL_Reg sysApi[] = {{"millis", api_millis}, {"memory", api_memory}, {nullptr, nullptr}};

/* State Setup (runs inside lua_pcall so memory errors are caught) --*/

static int setupState(lua_State* L) {
    // 1. LIBRARIES (no io, os, package, debug)
    static const luaL_Reg libs[] = {
        {LUA_GNAME, luaopen_base}, {LUA_COLIBNAME, luaopen_coroutine}, {LUA_TABLIBNAME, luaopen_table},
        {LUA_STRLIBNAME, luaopen_string}, {LUA_MATHLIBNAME, luaopen_math}, {LUA_UTF8LIBNAME, luaopen_utf8}, {nullptr, nullptr}};
    for (const luaL_Reg* lib = libs; lib->func != nullptr; lib++) {
        luaL_requiref(L, lib->name, lib->func, 1);
        lua_pop(L, 1);
    }

    // 2. SANDBOX (no file access, no binary chunks)
    static const char* removed[] = {"dofile", "loadfile", "load"};
    for (const char* name : removed) {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }
    lua_getglobal(L, LUA_STRLIBNAME);
    lua_pushnil(L);
    lua_setfield(L, -2, "dump");
    lua_pop(L, 1);

    // 3. DRUMBOY API
    lua_pushcfunction(L, api_print);
    lua_setglobal(L, "print");
    luaL_newlib(L, screenApi);
    lua_setglobal(L, "screen");
    luaL_newlib(L, sysApi);
    lua_setglobal(L, "sys");
    return 0;
}

/* Lua Plugin Implementation ----------------------------------------*/

void LuaPlugin::fail() {
    /// @brief Shows the error from the top of the stack and closes the state (frees its memory); the plugin stays off.

    char message[kLuaTextMaxLength + 32];
    snprintf(message, sizeof(message), "LUA ERROR %s: %s", idText, lua_isstring(L, -1) ? lua_tostring(L, -1) : "unknown");
    pluginHost_message(message, true);
    lua_close(L);
    L = nullptr;
}

bool LuaPlugin::call(const char* function, int nargs, uint32_t budgetMs) {
    /// @brief Calls a global function if the script defines it; arguments are already pushed.

    if (lua_getglobal(L, function) != LUA_TFUNCTION) {
        lua_pop(L, nargs + 1);
        return true;
    }
    lua_insert(L, -(nargs + 1));
    startBudget(budgetMs);
    if (lua_pcall(L, nargs, 0, 0) != LUA_OK) {
        fail();
        return false;
    }
    return true;
}

bool LuaPlugin::onStart() {
    /// @brief Creates the sandboxed state, loads the script from the SD card and calls on_load().

    // 1. STATE
    used = 0;
    L = lua_newstate(luaAlloc, this);
    if (L == nullptr) {
        char message[64];
        snprintf(message, sizeof(message), "LUA ERROR %s: not enough memory", idText);
        pluginHost_message(message, true);
        return false;
    }
    lua_sethook(L, budgetHook, LUA_MASKCOUNT, kLuaHookInstructions);
    lua_pushlightuserdata(L, this);
    lua_setfield(L, LUA_REGISTRYINDEX, "drumboy.plugin");
    lua_pushstring(L, idText);
    lua_setfield(L, LUA_REGISTRYINDEX, "drumboy.id");

    lua_pushcfunction(L, setupState);
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        fail();
        return false;
    }

    // 2. LOAD SCRIPT (text only)
    if (f_open(scriptFile, path, FA_READ) != FR_OK) {
        lua_pushstring(L, "cannot open file");
        fail();
        return false;
    }
    char chunkName[kLuaPluginIdSize + 8];
    snprintf(chunkName, sizeof(chunkName), "@%s.lua", idText);
    int loaded = lua_load(L, fileReader, nullptr, chunkName, "t");
    f_close(scriptFile);
    if (loaded != LUA_OK) {
        fail();
        return false;
    }

    // 3. RUN CHUNK + on_load()
    startBudget(kLuaLoadBudgetMs);
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        fail();
        return false;
    }
    return call("on_load", 0, kLuaLoadBudgetMs);
}

void LuaPlugin::onEvent(const PluginEvent& e) {
    if (L == nullptr) return;

    switch (e.type) {
        case PLUGIN_EVENT_STEP:
            lua_pushinteger(L, e.a | (e.b << 8));
            call("on_step", 1, kLuaEventBudgetMs);
            break;

        case PLUGIN_EVENT_NOTE:
            lua_pushinteger(L, e.a + 1);
            lua_pushinteger(L, e.b);
            call("on_note", 2, kLuaEventBudgetMs);
            break;

        case PLUGIN_EVENT_KEY:
            lua_pushinteger(L, e.a);
            lua_pushinteger(L, e.b);
            lua_pushboolean(L, e.c);
            call("on_key", 3, kLuaEventBudgetMs);
            break;

        case PLUGIN_EVENT_ENCODER:
            lua_pushinteger(L, e.a + 1);
            lua_pushinteger(L, (int8_t)e.b);
            call("on_encoder", 2, kLuaEventBudgetMs);
            break;

        case PLUGIN_EVENT_MIDI_IN:
            lua_pushinteger(L, e.a);
            call("on_midi_in", 1, kLuaEventBudgetMs);
            break;

        case PLUGIN_EVENT_MIDI_OUT:
            lua_pushinteger(L, e.a);
            lua_pushinteger(L, e.b);
            lua_pushinteger(L, e.c);
            call("on_midi_out", 3, kLuaEventBudgetMs);
            break;

        default:
            break;
    }
}

void LuaPlugin::onFrame(Lcd& lcd) {
    if (L == nullptr) return;

    frameLcd = &lcd;
    frameColor = WHITE;
    lcd.setFont(FONT_05x07);
    call("on_frame", 0, kLuaFrameBudgetMs);
    frameLcd = nullptr;
}

/* Discovery --------------------------------------------------------*/

void registerLuaPlugins(FIL& file) {
    /// @brief Registers every visible Plugins/*.lua file as a (not yet started) plugin.

    static DIR dir;
    static FILINFO info;
    scriptFile = &file;
    luaHeap_initialize();

    if (f_opendir(&dir, kLuaPluginFolder) != FR_OK) return;
    while ((luaPluginCount < kLuaPluginMaxCount) && (f_readdir(&dir, &info) == FR_OK) && (info.fname[0] != '\0')) {
        const char* name = info.fname;
        size_t len = strlen(name);
        if ((info.fattrib & (AM_DIR | AM_HID)) || (name[0] == '.') || (len <= 4)) continue;
        if (strcasecmp(&name[len - 4], ".lua") != 0) continue;

        LuaPlugin& plugin = luaPlugins[luaPluginCount++];
        plugin.setup(name);
        pluginHost_register(&plugin);
    }
    f_closedir(&dir);
}

#endif
