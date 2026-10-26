#ifndef __LCD_H
#define __LCD_H

#include <stdio.h>
#include <stdlib.h>

#include "Font_05x07.h"
#include "Font_07x09.h"
#include "Font_10x14.h"
#include "Font_14x18.h"
#include "Global.h"
#include "Icon.h"
#include "NT35510.h"
#include "main.h"
#include "string.h"

// clang-format off

class Lcd {
   private:
    /* Panel State ------------------------------------------------ */

    uint16_t width;                                                     // Active logical panel width
    uint16_t height;                                                    // Active logical panel height
    RGB16Color foreColor;                                               // Current drawing foreground color
    RGB16Color backColor;                                               // Current drawing background color
    LcdRotation rotation;                                               // Current LCD rotation mode

    /* Font Rendering State -------------------------------------- */

    LcdFont font;                                                       // Active font preset
    LcdAlign fontAlignment;                                             // Current text alignment mode
    const uint16_t* fontData;                                           // Pointer to selected font bitmap table
    uint16_t fontSpacing;                                               // Extra pixel spacing between characters
    uint16_t fontWidth;                                                 // Glyph width for selected font
    uint16_t fontHeight;                                                // Glyph height for selected font

    /* Low-Level Bus Primitives ---------------------------------- */

    ALWAYSINLINE void writeCommand(uint16_t command) {
        LCD_RS_GPIO_Port->BSRR = (uint32_t)LCD_RS_Pin << 16U;
        LCD_DATA_GPIO_Port->ODR = command;
        LCD_WR_GPIO_Port->BSRR = (uint32_t)LCD_WR_Pin << 16U;           // Low
        LCD_WR_GPIO_Port->BSRR = (uint32_t)LCD_WR_Pin;                  // High
    }

    ALWAYSINLINE void writeData(uint16_t data) {
        LCD_RS_GPIO_Port->BSRR = (uint32_t)LCD_RS_Pin;
        LCD_DATA_GPIO_Port->ODR = data;
        LCD_WR_GPIO_Port->BSRR = (uint32_t)LCD_WR_Pin << 16U;           // Low
        LCD_WR_GPIO_Port->BSRR = (uint32_t)LCD_WR_Pin;                  // High
    }

    /* Private Helper Functions ---------------------------------- */

    void setAddressWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
    void clearAddressWindow();
    void fastFill(RGB16Color color, uint32_t pix);
    uint8_t getLetter(char letter);
    RGB16Color alphaColor(RGB16Color color_, uint8_t alpha);
    RGB16Color alphaColor(struct RGB24RawColor color_, uint8_t alpha);
    RGB16Color convertColor(struct RGB24RawColor color_);

   public:
    /* Lifecycle -------------------------------------------------- */

    Lcd(LcdRotation rotation_, RGB16Color foreColor_, RGB16Color backColor_, LcdFont font_);
    ~Lcd();

    /* State Accessors ------------------------------------------- */

    uint16_t getWidth() const { return width; }
    uint16_t getHeight() const { return height; }
    void setForeColor(RGB16Color color_) { foreColor = color_; }
    RGB16Color getForeColor() const { return foreColor; }
    void setBackColor(RGB16Color color_) { backColor = color_; }
    RGB16Color getBackColor() const { return backColor; }
    void setRotation(LcdRotation rotation_);
    LcdRotation getRotation() const { return rotation; }
    void setFont(LcdFont font_);
    LcdFont getFont() const { return font; }
    void setSpacing(uint16_t spacing_) { fontSpacing = spacing_; }
    uint16_t getSpacing() const { return fontSpacing; }
    void setAlignment(LcdAlign align_) { fontAlignment = align_; }
    LcdAlign getAlignment() const { return fontAlignment; }

    /* Pixel Streaming (plugins) --------------------------------- */
    // Set a window once, then push all of its pixels left to right, top to bottom
    // (same bus sequence as fastFill, one colour per pixel). Always end with endStream().

    void beginStream(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
        setAddressWindow(x0, y0, x1, y1);
        LCD_CS_LOW;
    }
    ALWAYSINLINE void streamPixel(RGB16Color color) {
        LCD_DATA_GPIO_Port->ODR = color;
        LCD_WR_GPIO_Port->BSRR = (uint32_t)LCD_WR_Pin << 16U;           // Low
        LCD_WR_GPIO_Port->BSRR = (uint32_t)LCD_WR_Pin;                  // High
    }
    void endStream() { LCD_CS_HIGH; }

    /* Core Display Functions ------------------------------------ */

    void initialize();
    void clearScreen();
    void fillScreen();
    void fillScreen(RGB16Color color);

    /* Primitive Drawing Functions ------------------------------- */

    void drawPixel(uint16_t x, uint16_t y);
    void drawHLine(uint16_t x, uint16_t y, int16_t l);
    void drawVLine(uint16_t x, uint16_t y, int16_t l);
    void drawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
    void drawRect(uint16_t x, uint16_t y, uint32_t w, uint32_t h);
    void fillRect(uint16_t x, uint16_t y, uint32_t w, uint32_t h);
    void drawCircle(uint16_t x, uint16_t y, uint16_t r);
    void drawCircleHelper(uint16_t x, uint16_t y, uint16_t r, uint8_t cornername);
    void drawQuarterCircle(uint16_t x, uint16_t y, uint16_t r, uint8_t quarter);
    void drawHalfCircle(uint16_t x, uint16_t y, uint16_t r, uint8_t half);
    void fillCircle(uint16_t x, uint16_t y, uint16_t r);
    void fillCircleHelper(uint16_t x, uint16_t y, uint16_t r, uint8_t cornername, uint16_t delta);
    void drawTriangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t x3, uint16_t y3);
    void fillTriangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t x3, uint16_t y3);

    /* Primitive Clear Functions --------------------------------- */

    void clearHLine(uint16_t x, uint16_t y, uint16_t l);
    void clearVLine(uint16_t x, uint16_t y, uint16_t l);
    void clearRect(uint16_t x, uint16_t y, uint32_t w, uint32_t h);

    /* Image Drawing Functions ----------------------------------- */

    void drawRGB16Image(const RGB16Color* indexPtr, const uint8_t* pixelPtr, uint8_t indexSize, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
    void drawRGB16Image(const RGB16Color* indexPtr, const uint8_t* pixelPtr, uint8_t indexSize, uint8_t alpha, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
    void drawRGB16ImagePartial(const RGB16Color* indexPtr, const uint8_t* pixelPtr, uint8_t indexSize, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t xStart, uint16_t xWidth);
    void drawRGB24Image(const RGB24RawColor* indexPtr, const uint8_t* pixelPtr, uint8_t indexSize, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
    void drawRGB24Image(const RGB24RawColor* indexPtr, const uint8_t* pixelPtr, uint8_t indexSize, uint8_t alpha, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
    void fadeRGB16Image(const RGB16Color* indexPtr, const uint8_t* pixelPtr, uint8_t indexSize, uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool mode, uint8_t step, uint16_t delay);
    void fadeRGB24Image(const RGB24RawColor* indexPtr, const uint8_t* pixelPtr, uint8_t indexSize, uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool mode, uint8_t step, uint16_t delay);

    /* Startup Alert Functions ----------------------------------- */

    void drawInitSdAlert(SdResult result);
    void drawInitSdReadAlert();
    void clearInitSdAlert();

    /* Text Rendering Functions ---------------------------------- */

    void drawText(const char text[], uint16_t textLength, uint16_t x, uint16_t y);
    void drawNumber(int32_t num, uint8_t numSize, uint16_t x, uint16_t y);

    /* Display Control Functions --------------------------------- */

    void invertDisplay(bool invert);
    void displayOn();
    void displayOff();
};

// clang-format on

#endif
