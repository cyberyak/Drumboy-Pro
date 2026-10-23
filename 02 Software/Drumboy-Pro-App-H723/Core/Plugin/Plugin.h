#ifndef __PLUGIN_H
#define __PLUGIN_H

#include "PluginHooks.h"

#ifdef DRUMBOY_PLUGINS

class Lcd;

/* Plugin Interface -------------------------------------------------*/
// Built-in (compiled-in) plugins implement this interface. The Lua runtime
// will later be one more Plugin that forwards these calls to scripts.
// All methods run in the main loop, never in interrupt context.

class Plugin {
   public:
    virtual const char* name() const = 0;

    virtual void onEvent(const PluginEvent& event) { (void)event; }    // Called once per queued event
    virtual void onFrame(Lcd& lcd) { (void)lcd; }                      // Called at kPluginFrameInterval

   protected:
    ~Plugin() {}
};

void pluginHost_register(Plugin* plugin);
uint32_t pluginHost_getDroppedEvents();

#endif

#endif
