#include "Plugin.h"

#ifdef DRUMBOY_PLUGINS

#include <ctype.h>
#include <strings.h>

#include "Lcd.h"
#include "ff.h"

// clang-format off

/* Settings ---------------------------------------------------------*/

const uint16_t kPluginQueueSize                     = 128;              // Power of two
const uint8_t  kPluginMaxCount                      = 16;               // Built-ins + Lua scripts
const uint8_t  kPluginMaxEventsPerUpdate            = 32;               // Bounds main-loop time per pass
const uint32_t kPluginFrameInterval                 = 33;               // ms (~30 fps)
const char     kPluginConfigPath[]                  = "Plugins/plugins.txt";
const uint16_t kPluginConfigMaxSize                 = 512;
const uint32_t kPluginMessageTime                   = 6000;             // ms a message stays on screen
const uint16_t kPluginMessageX                      = 20;
const uint16_t kPluginMessageY                      = 16;               // Second line of the empty top strip
const uint16_t kPluginMessageWidth                  = 920;
const uint8_t  kPluginMessageSize                   = 140;

// clang-format on

/* Event Queue (multi-producer: ISRs + main, single consumer: main) -*/

static PluginEvent queue[kPluginQueueSize];
static volatile uint16_t queueHead = 0;
static volatile uint16_t queueTail = 0;
static volatile uint32_t droppedEvents = 0;

/* Registry ---------------------------------------------------------*/

static Plugin* plugins[kPluginMaxCount];
static bool pluginEnabled[kPluginMaxCount];
static uint8_t pluginCount = 0;
static uint8_t enabledCount = 0;
static uint32_t lastFrameTick = 0;

/* Message Line -----------------------------------------------------*/

static char messageText[kPluginMessageSize] = "";
static bool messageError = false;
static bool messagePending = false;
static bool messageShown = false;
static uint32_t messageTick = 0;

void registerBuiltinPlugins();         // BuiltinPlugins.cpp
#ifdef DRUMBOY_LUA
void registerLuaPlugins(FIL& file);    // LuaPlugin.cpp
#endif

void pluginHost_message(const char* text, bool error) {
    /// @brief Queues a one-line message (Lua print() or error) for the top strip; the latest one wins.

    strncpy(messageText, text, sizeof(messageText) - 1);
    messageText[sizeof(messageText) - 1] = '\0';
    for (char* c = messageText; *c != '\0'; c++) {
        if ((*c == '\n') || (*c == '\t')) *c = ' ';
    }
    messageError = error;
    messagePending = true;
}

static void drawMessage(Lcd& lcd) {
    /// @brief Draws a pending message, and clears it after kPluginMessageTime. Caller guards LCD state.

    uint32_t tick = HAL_GetTick();
    if (messagePending || (messageShown && ((tick - messageTick) >= kPluginMessageTime))) {
        lcd.setBackColor(BLACK);
        lcd.clearRect(kPluginMessageX, kPluginMessageY - 1, kPluginMessageWidth, 9);
        messageShown = false;
    }
    if (messagePending) {
        lcd.setFont(FONT_05x07);
        lcd.setAlignment(LEFT);
        lcd.setForeColor(messageError ? RED : WHITE);
        lcd.drawText(messageText, strlen(messageText), kPluginMessageX, kPluginMessageY);
        messagePending = false;
        messageShown = true;
        messageTick = tick;
    }
}

static bool isEnabledValue(const char* value) {
    return (strcasecmp(value, "on") == 0) || (strcasecmp(value, "yes") == 0) || (strcasecmp(value, "true") == 0) || (strcmp(value, "1") == 0);
}

static char* trim(char* text) {
    while (isspace((unsigned char)*text)) text++;
    char* end = text + strlen(text);
    while ((end > text) && isspace((unsigned char)end[-1])) *--end = '\0';
    return text;
}

static void loadConfig(FIL& file) {
    /// @brief Reads "id = on|off" lines from Plugins/plugins.txt; a missing file leaves every plugin disabled.

    // 1. READ FILE (static, 32-byte aligned buffer in RAM_D1 where SDMMC DMA can reach it)
    static char text[kPluginConfigMaxSize + 1] __attribute__((aligned(32)));
    UINT bytesRead = 0;

    if (f_open(&file, kPluginConfigPath, FA_READ) != FR_OK) return;
    FRESULT result = f_read(&file, text, kPluginConfigMaxSize, &bytesRead);
    f_close(&file);
    if (result != FR_OK) return;
    text[bytesRead] = '\0';

    // 2. PARSE LINES
    char* line = text;
    while (line != nullptr) {
        char* next = strpbrk(line, "\r\n");
        if (next != nullptr) *next++ = '\0';

        char* hash = strchr(line, '#');
        if (hash != nullptr) *hash = '\0';

        char* equals = strchr(line, '=');
        if (equals != nullptr) {
            *equals = '\0';
            const char* key = trim(line);
            const char* value = trim(equals + 1);

            for (uint8_t i = 0; i < pluginCount; i++) {
                if (strcasecmp(key, plugins[i]->id()) == 0) pluginEnabled[i] = isEnabledValue(value);
            }
        }
        line = next;
    }
}

void pluginHost_initialize(FIL& file) {
    /// @brief Registers built-in and Lua plugins, applies Plugins/plugins.txt and starts the enabled ones.
    /// Called once after controller.initialize(); borrows the controller's idle FIL (4 KB).

    // 1. DISCOVER
    registerBuiltinPlugins();
#ifdef DRUMBOY_LUA
    registerLuaPlugins(file);
#endif

    // 2. CONFIGURE
    loadConfig(file);

    // 3. START ENABLED PLUGINS (a failed start leaves the plugin off)
    enabledCount = 0;
    for (uint8_t i = 0; i < pluginCount; i++) {
        if (pluginEnabled[i] && !plugins[i]->onStart()) pluginEnabled[i] = false;
        if (pluginEnabled[i]) enabledCount++;
    }
}

void pluginHost_register(Plugin* plugin) {
    if ((plugin != nullptr) && (pluginCount < kPluginMaxCount)) {
        plugins[pluginCount] = plugin;
        pluginEnabled[pluginCount] = false;
        pluginCount++;
    }
}

uint32_t pluginHost_getDroppedEvents() {
    return droppedEvents;
}

void pluginHost_push(PluginEventType type, uint8_t a, uint8_t b, uint8_t c) {
    /// @brief Enqueues an event; safe from any interrupt priority and from already-masked sections.

    // 1. CRITICAL SECTION (restore previous mask state, callers may already hold one)
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    // 2. OVERFLOW GUARD
    uint16_t nextHead = (queueHead + 1) & (kPluginQueueSize - 1);
    if (nextHead == queueTail) {
        droppedEvents++;
        __set_PRIMASK(primask);
        return;
    }

    // 3. ENQUEUE
    PluginEvent& event = queue[queueHead];
    event.type = type;
    event.a = a;
    event.b = b;
    event.c = c;
    event.tick = HAL_GetTick();
    queueHead = nextHead;

    __set_PRIMASK(primask);
}

void pluginHost_pushKeypad(uint8_t keypad, const uint8_t* changeState, uint8_t count) {
    /// @brief Converts a keypad change-state array (KEYSTATE_*) into KEY events.

    for (uint8_t i = 0; i < count; i++) {
        if (changeState[i] == KEYSTATE_PRESS) {
            pluginHost_push(PLUGIN_EVENT_KEY, keypad, i, 1);
        } else if (changeState[i] == KEYSTATE_RELEASE) {
            pluginHost_push(PLUGIN_EVENT_KEY, keypad, i, 0);
        }
    }
}

void pluginHost_update(Lcd& lcd) {
    /// @brief Main-loop pass: dispatches queued events, then frames at kPluginFrameInterval.

    // 1. EVENT DISPATCH (bounded per pass so the UI stays responsive)
    for (uint8_t n = 0; (n < kPluginMaxEventsPerUpdate) && (queueTail != queueHead); n++) {
        PluginEvent event = queue[queueTail];
        queueTail = (queueTail + 1) & (kPluginQueueSize - 1);

        for (uint8_t i = 0; i < pluginCount; i++) {
            if (pluginEnabled[i] && plugins[i]->isActive()) plugins[i]->onEvent(event);
        }
    }

    // 2. FRAME
    if ((enabledCount == 0) && !messagePending && !messageShown) return;
    uint32_t tick = HAL_GetTick();
    if ((tick - lastFrameTick) < kPluginFrameInterval) return;
    lastFrameTick = tick;

    // 3. LCD STATE GUARD
    // Plugins may change font/colours freely; the stock UI's drawing state is restored afterwards.
    LcdFont font = lcd.getFont();
    uint16_t spacing = lcd.getSpacing();
    LcdAlign align = lcd.getAlignment();
    RGB16Color fore = lcd.getForeColor();
    RGB16Color back = lcd.getBackColor();

    for (uint8_t i = 0; i < pluginCount; i++) {
        if (pluginEnabled[i] && plugins[i]->isActive()) plugins[i]->onFrame(lcd);
    }
    drawMessage(lcd);

    lcd.setFont(font);
    lcd.setSpacing(spacing);
    lcd.setAlignment(align);
    lcd.setForeColor(fore);
    lcd.setBackColor(back);
}

#endif
