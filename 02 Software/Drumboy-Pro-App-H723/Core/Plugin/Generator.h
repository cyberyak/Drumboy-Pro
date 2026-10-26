#ifndef __GENERATOR_H
#define __GENERATOR_H

#include "PluginHooks.h"

#ifdef DRUMBOY_PLUGINS

class Controller;

/* Pattern Generator Page -------------------------------------------*/
// Opened with FILL + STYLE (hold FILL, press STYLE) from any menu. Owns key
// presses and knobs while open (transport keys still work), but not the
// screen: the song lanes keep showing the live pattern. Parameters are shown
// in the top strip; generated steps carry per-step probability and velocity.

void generator_initialize();
bool generator_isOpen();
bool generator_needsFrame();
bool generator_ownsStrip();                                             // Open or clearing: plugin lines and messages wait
bool generator_keyPasses(uint8_t keypad, uint8_t key);
bool generator_handleEvent(const PluginEvent& event, Controller& controller);  // True if the event was used
void generator_frame(Controller& controller);

#endif

#endif
