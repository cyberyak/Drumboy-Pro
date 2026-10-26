#ifndef __PLUGIN_H
#define __PLUGIN_H

#include "PluginHooks.h"

#ifdef DRUMBOY_PLUGINS

#include "ff.h"

class Lcd;

// clang-format off

/* Show Canvas ------------------------------------------------------*/
// The layer-row area a show-mode plugin owns (rows 1-10, full width).

const uint16_t kShowX                               = 20;               // kImageLayerX
const uint16_t kShowY                               = 131;              // kImageLayerY[0]
const uint16_t kShowWidth                           = 920;              // kImageLayerWidth
const uint16_t kShowHeight                          = 310;              // 10 rows x kImageLayerHeight

// clang-format on

/* Plugin Interface -------------------------------------------------*/
// Built-in (compiled-in) plugins implement this interface.
// All methods run in the main loop, never in interrupt context.
// Disabled plugins receive no calls at all.

class Plugin {
   public:
    virtual const char* id() const = 0;                                 // Key in Plugins/plugins.txt, e.g. "cpu_meter"
    virtual const char* name() const = 0;                               // Display name
    virtual const char* kind() const { return "BUILT-IN"; }             // Shown in the manager

    virtual bool onStart() { return true; }                             // Once at boot when enabled; false = stays off
    virtual bool isActive() const { return true; }                      // False after a runtime failure
    virtual void onEvent(const PluginEvent& event) { (void)event; }    // Called once per queued event
    virtual void onFrame(Lcd& lcd) { (void)lcd; }                      // Called at kPluginFrameInterval

    // Show mode: the plugin owns the show canvas (started from the manager with ADD, ended with FILE).
    // While showing, it also receives the keys and knobs the stock UI would otherwise get.
    virtual bool canShow() const { return false; }
    virtual void onShowStart() {}
    virtual void onShowFrame(Lcd& lcd) { (void)lcd; }                  // Every frame; the canvas is cleared to black once at start
    virtual void onShowEnd() {}

   protected:
    ~Plugin() {}
};

/* Audio Snapshot ---------------------------------------------------*/
// Output peaks since the previous frame, 0..1, taken once per plugin frame.

struct PluginAudio {
    float level;                                                        // Full band
    float bass;                                                         // Below ~150 Hz
    float treble;                                                       // Above ~2 kHz
};

const PluginAudio& pluginHost_getAudio();

void pluginHost_register(Plugin* plugin);
uint32_t pluginHost_getDroppedEvents();
void pluginHost_message(const char* text, bool error);                  // Shown in the top strip for a few seconds

/* Registry access for the Plugin & Firmware Manager ----------------*/

uint8_t pluginHost_getCount();
Plugin* pluginHost_getPlugin(uint8_t index);
bool pluginHost_isConfigured(uint8_t index);                            // "on" in plugins.txt (or changed in the manager)
bool pluginHost_isRunning(uint8_t index);                               // Started at boot and still active
void pluginHost_setConfigured(uint8_t index, bool on);                  // Takes effect after a restart
bool pluginHost_saveConfig(FIL& file);                                  // Rewrites Plugins/plugins.txt

#endif

#endif
