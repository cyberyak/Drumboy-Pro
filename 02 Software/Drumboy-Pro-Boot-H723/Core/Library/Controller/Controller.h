#ifndef __CONTROLLER_H
#define __CONTROLLER_H

#include <cctype>

#include "Dac.h"
#include "Global.h"
#include "Lcd.h"
#include "fatfs.h"
#include "main.h"
#include "sdmmc.h"
#include "stm32h7xx_hal.h"
#include "string.h"

// clang-format off

/* Firmware Buffers --------------------------------------- */

extern "C" uint8_t dataSd[1024];
extern "C" uint8_t dataFlash[1024];

class Controller {
   private:

    /* Internal Flags --------------------------------------- */

    bool sdBusy;
    bool sdInsertCheck;

    bool power;
    bool lcdTest;

    bool lcdActive;   // LCD has been initialized and switched on
    bool flashDirty;  // application flash was erased/written but not verified

   public:

    /* Lifecycle -------------------------------------------- */

    Controller();
    ~Controller();

    /* Hardware Peripherals --------------------------------- */

    Lcd lcd;
    Dac dac;
    Sd sd;

    /* State Accessors (Getters/Setters) -------------------- */

    bool getPower() { return power; }
    void setPower(bool power_) { power = power_; }

    bool getLcdTest() { return lcdTest; }
    void setLcdTest(bool test_) { lcdTest = test_; }

    void initialize();
    void updateFirmware();
    bool isApplicationValid();
    void startApplication();
    void haltNoApplication();

    /* Crc Functions ------------------------------------------ */

    uint32_t crc_update(uint32_t initial, const void* buf, uint32_t len);

    /* Flash Functions -------------------------------------- */

    uint32_t flash_getSector(uint32_t address);
    uint32_t flash_eraseSector(uint32_t startSectorAddress, uint32_t byteSize);
    uint32_t flash_writeData(uint32_t StartSectorAddress, uint32_t* data, uint32_t byteSize);
    void flash_readData(uint32_t StartSectorAddress, uint32_t* data, uint32_t wordSize);

    /* Dac Functions ---------------------------------------- */

    void dac_initialize();

    /* Sd Functions ----------------------------------------- */

    void sd_initialize();
    SdResult sd_detect();
    SdResult sd_mount();
    SdResult sd_unmount();
    SdResult sd_getSpace();
    SdResult sd_checkFileExist(const char* fileAddress);
    SdResult sd_checkFolderExist(const char* folderAddress);
    FRESULT sd_createDirectory(const char* path);
    FRESULT sd_deleteDirectory(const char* path);

    /* Lcd Functions ---------------------------------------- */

    void lcd_initialize();

    /* Interrupt Functions ---------------------------------- */

    void interruptSd();

    /* Debug Functions -------------------------------------- */

    void check(int32_t num_, uint8_t line_) {
        lcd.setAlignment(LEFT);
        lcd.setFont(FONT_05x07);
        lcd.setForeColor(WHITE);
        lcd.setBackColor(BLACK);
        lcd.drawNumber(num_, 8, 30, 30 + (line_ * 10));
    }

    void test();
};

// clang-format on

#endif
