#ifndef __BRANDING_H
#define __BRANDING_H

#include <stdint.h>

// clang-format off

/* Build Label ------------------------------------------------------*/
// Custom firmware identity: "<base>-cy.<build>", e.g. "1.01-cy.1" = stock
// Randomwaves 1.01 (kVersionMajor/kVersionMinor in Global.h) + cyberyak
// build 1 (DRUMBOY_BUILD_NUMBER, from the VERSION file via CMake).
// The label is drawn in the empty top-right corner of the screen while the
// File menu is open, and cleared again when it closes.
// Enabled with -DDRUMBOY_BRANDING (on in all presets except "Stock").

class Lcd;

const char* branding_getVersionString();

#ifdef DRUMBOY_BRANDING

void branding_draw(Lcd& lcd, bool visible);

#define BRANDING_DRAW(lcd_, visible_)               branding_draw((lcd_), (visible_))

#else

#define BRANDING_DRAW(lcd_, visible_)               ((void)0)

#endif

// clang-format on

#endif
