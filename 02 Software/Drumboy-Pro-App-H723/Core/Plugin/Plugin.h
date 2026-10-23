#ifndef __PLUGIN_H
#define __PLUGIN_H

#include "PluginHooks.h"

#ifdef DRUMBOY_PLUGINS

class Lcd;

/* Plugin Interface -------------------------------------------------*/
// Built-in (compiled-in) plugins implement this interface. The Lua runtime
// will later be one more Plugin that forwards these calls to scripts.
// All methods run in the main loop, never in interrupt context.
// Disabled plugins receive no calls at all.

class Plugin {
   public:
    virtual const char* id() const = 0;                                 // Key in Plugins/plugins.txt, e.g. "cpu_meter"
    virtual const char* name() const = 0;                               // Display name

    virtual bool onStart() { return true; }                             // Once at boot when enabled; false = stays off
    virtual bool isActive() const { return true; }                      // False after a runtime failure
    virtual void onEvent(const PluginEvent& event) { (void)event; }    // Called once per queued event
    virtual void onFrame(Lcd& lcd) { (void)lcd; }                      // Called at kPluginFrameInterval

   protected:
    ~Plugin() {}
};

void pluginHost_register(Plugin* plugin);
uint32_t pluginHost_getDroppedEvents();
void pluginHost_message(const char* text, bool error);                  // Shown in the top strip for a few seconds

#endif

#endif
