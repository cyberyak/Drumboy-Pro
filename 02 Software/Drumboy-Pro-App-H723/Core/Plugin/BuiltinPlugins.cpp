#include "Plugin.h"

#ifdef DRUMBOY_PLUGINS

/* Built-in Plugins -------------------------------------------------*/
// Compiled-in plugins, all disabled until switched on in Plugins/plugins.txt.
// Registration order is drawing order.

Plugin* builtin_cpuMeter();
Plugin* builtin_hookMonitor();
Plugin* builtin_psychedelic();

void registerBuiltinPlugins() {
    pluginHost_register(builtin_cpuMeter());
    pluginHost_register(builtin_hookMonitor());
    pluginHost_register(builtin_psychedelic());
}

#endif
