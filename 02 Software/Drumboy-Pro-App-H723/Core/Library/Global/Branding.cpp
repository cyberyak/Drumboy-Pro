#include "Branding.h"

#include "Lcd.h"

// clang-format off

#ifndef DRUMBOY_BUILD_NUMBER
#define DRUMBOY_BUILD_NUMBER                        0
#endif

const char kBrandingName[]                          = "CYBERYAK";
const uint32_t kBrandingDrawInterval                = 1000;             // ms; stock full-screen redraws may clear the label
const uint16_t kBrandingTextX                       = 940;              // Right-aligned in the empty top-right corner
const uint16_t kBrandingTextY                       = 6;
const uint16_t kBrandingClearWidth                  = 200;              // Covers the longest label; the strip is empty in stock layouts
const uint16_t kBrandingClearHeight                 = 9;

// clang-format on

const char* branding_getVersionString() {
    /// @brief Returns "<major>.<minor>-cy.<build>", built once from Global.h and the build number.

    static char text[16] = "";
    if (text[0] == '\0') {
        snprintf(text, sizeof(text), "%u.%02u-cy.%u", kVersionMajor, kVersionMinor, (unsigned)DRUMBOY_BUILD_NUMBER);
    }
    return text;
}

#ifdef DRUMBOY_BRANDING

static bool shown = false;
static uint32_t lastDrawTick = 0;

void branding_draw(Lcd& lcd, bool visible) {
    /// @brief Shows the build label while visible (redrawn every kBrandingDrawInterval ms) and clears it once hidden.

    // 1. STATE CHECK
    uint32_t tick = HAL_GetTick();
    if (visible) {
        if (shown && ((tick - lastDrawTick) < kBrandingDrawInterval)) return;
    } else if (!shown) {
        return;
    }

    // 2. SAVE LCD STATE
    LcdFont font = lcd.getFont();
    uint16_t spacing = lcd.getSpacing();
    LcdAlign align = lcd.getAlignment();
    RGB16Color fore = lcd.getForeColor();
    RGB16Color back = lcd.getBackColor();

    lcd.setFont(FONT_05x07);
    lcd.setBackColor(BLACK);

    // 3. DRAW OR CLEAR
    if (visible) {
        char text[32];
        int len = snprintf(text, sizeof(text), "%s %s", kBrandingName, branding_getVersionString());
        if (len >= (int)sizeof(text)) len = sizeof(text) - 1;
        if (len > 0) {
            lcd.setAlignment(RIGHT);
            lcd.setForeColor(GRAY);
            lcd.drawText(text, len, kBrandingTextX, kBrandingTextY);
        }
        shown = true;
        lastDrawTick = tick;
    } else {
        lcd.clearRect(kBrandingTextX - kBrandingClearWidth, kBrandingTextY - 1, kBrandingClearWidth + 1, kBrandingClearHeight);
        shown = false;
    }

    // 4. RESTORE LCD STATE
    lcd.setFont(font);
    lcd.setSpacing(spacing);
    lcd.setAlignment(align);
    lcd.setForeColor(fore);
    lcd.setBackColor(back);
}

#endif
