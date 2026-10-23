#ifndef __MANAGER_H
#define __MANAGER_H

#include "PluginHooks.h"

#ifdef DRUMBOY_PLUGINS

class Controller;

/* Plugin & Firmware Manager ----------------------------------------*/
// Opened by pressing FILE again while the File menu is shown. Draws only
// inside the 10 song lanes (restored with lcd_drawSong() on close) and owns
// keys and knobs while open; audio and the sequencer keep running.

bool manager_isOpen();
bool manager_needsFrame();                                              // Open, or lanes still to restore
bool manager_handleEvent(const PluginEvent& event, Controller& controller);  // True if the event was used
void manager_frame(Controller& controller);                             // Called at the plugin frame rate
void manager_endUpdate(Controller& controller);                         // Called at the end of every main-loop pass

#endif

#endif
