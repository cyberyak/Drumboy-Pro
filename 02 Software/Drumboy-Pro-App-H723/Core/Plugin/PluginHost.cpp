#include "Plugin.h"

#ifdef DRUMBOY_PLUGINS

#include "Lcd.h"

// clang-format off

/* Settings ---------------------------------------------------------*/

const uint16_t kPluginQueueSize                     = 128;              // Power of two
const uint8_t  kPluginMaxCount                      = 8;
const uint8_t  kPluginMaxEventsPerUpdate            = 32;               // Bounds main-loop time per pass
const uint32_t kPluginFrameInterval                 = 33;               // ms (~30 fps)

// clang-format on

/* Event Queue (multi-producer: ISRs + main, single consumer: main) -*/

static PluginEvent queue[kPluginQueueSize];
static volatile uint16_t queueHead = 0;
static volatile uint16_t queueTail = 0;
static volatile uint32_t droppedEvents = 0;

/* Registry ---------------------------------------------------------*/

static Plugin* plugins[kPluginMaxCount];
static uint8_t pluginCount = 0;
static uint32_t lastFrameTick = 0;

void registerBuiltinPlugins();  // BuiltinPlugins.cpp

void pluginHost_initialize() {
    /// @brief Registers compiled-in plugins. Called once after controller.initialize().

    registerBuiltinPlugins();
}

void pluginHost_register(Plugin* plugin) {
    if ((plugin != nullptr) && (pluginCount < kPluginMaxCount)) plugins[pluginCount++] = plugin;
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

        for (uint8_t i = 0; i < pluginCount; i++) plugins[i]->onEvent(event);
    }

    // 2. FRAME
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

    for (uint8_t i = 0; i < pluginCount; i++) plugins[i]->onFrame(lcd);

    lcd.setFont(font);
    lcd.setSpacing(spacing);
    lcd.setAlignment(align);
    lcd.setForeColor(fore);
    lcd.setBackColor(back);
}

#endif
