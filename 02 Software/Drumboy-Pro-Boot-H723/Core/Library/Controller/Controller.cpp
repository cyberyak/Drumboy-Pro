#include "Controller.h"

////////////////////////////////////////////////////////////////////////////////
/* Lifecycle Functions -------------------------------------------------------*/
////////////////////////////////////////////////////////////////////////////////

Controller::Controller()
    : lcd(LANDSCAPE_1, WHITE, BLACK, FONT_07x09),
      dac(),
      sd(),

      sdBusy(false),
      sdInsertCheck(false),

      power(false),

      lcdTest(false),

      lcdActive(false),
      flashDirty(false) {
}

Controller::~Controller() {}

void Controller::initialize() {
    /// @brief Initializes boot workflow and attempts firmware update before app jump.

    // 1. BOOT STATUS INDICATOR
    // Light boot LED while update/startup flow is running.
    LED0_ON;

    // 2. FIRMWARE UPDATE FLOW
    // Mount SD and process firmware package if present.
    sd_initialize();
    updateFirmware();

    // 3. APPLICATION JUMP
    // Only jump when the application flash is intact. After a failed program/verify the
    // image may be erased or partial, so stay in the bootloader and tell the user instead
    // of jumping into garbage (the package is kept on the SD card so the next boot retries).
    if (flashDirty || !isApplicationValid()) {
        haltNoApplication();
    }
    startApplication();
}

void Controller::updateFirmware() {
    /// @brief Scans SD card for firmware package and performs in-place flash update.

    // 1. DEFAULT RESULT STATE
    // Assume failure until the full validation/programming pipeline succeeds.
    FwResult fwResult = FW_ERROR;

    // 2. SD DETECT + MOUNT
    // Continue only when card is present and filesystem is mounted.
    if ((sd_detect() == SD_OK) && (sd_mount() == SD_OK)) {
        sd.fresult = f_findfirst(&sd.dir, &sd.fileInfo, "", "Drumboy_Pro_Fw?*.bin");

        // 3. FIRMWARE FILE DISCOVERY
        // Search expected firmware naming pattern and open the first candidate.
        if (sd.fileInfo.fname[0] != '\0') {
            if (sd_checkFileExist(sd.fileInfo.fname) == SD_OK) {
                if (f_open(&sd.file, sd.fileInfo.fname, FA_READ) == FR_OK) {
                    char data[40] = "";
                    char headerTitle[] = "RW_DRUMBOY_PRO_FIRMWARE ";

                    // 4. HEADER VALIDATION
                    // Verify file signature before reading metadata.
                    if ((f_read(&sd.file, data, 24, &sd.bytesread) == FR_OK) && (sd.bytesread == 24)) {
                        lcd.initialize();
                        lcd.displayOn();
                        lcdActive = true;

                        HAL_Delay(500);

                        if (strcmp(data, headerTitle) == 0) {
                            memset(data, 0x00, sizeof(data));

                            // 5. METADATA LOAD + RANGE CHECKS
                            // Read version/size/CRC and validate compatibility/limits.
                            // Fields are copied out with memcpy: casting into the char buffer is
                            // unaligned access and breaks strict aliasing.
                            if ((f_read(&sd.file, data, 12, &sd.bytesread) == FR_OK) && (sd.bytesread == 12)) {
                                uint16_t versionMajor;
                                uint16_t versionMinor;
                                uint32_t firmwareSize;
                                uint32_t crcRef;
                                memcpy(&versionMajor, &data[0], sizeof(versionMajor));
                                memcpy(&versionMinor, &data[2], sizeof(versionMinor));
                                memcpy(&firmwareSize, &data[4], sizeof(firmwareSize));
                                memcpy(&crcRef, &data[8], sizeof(crcRef));

                                const uint32_t maxFirmwareSize = FLASH_FIRMWARE_END_ADDRESS - FLASH_FIRMWARE_ADDRESS;
                                uint32_t readSize = 10240;
                                uint32_t fileSize = f_size(&sd.file);
                                uint16_t readCount = firmwareSize / readSize;
                                uint16_t remainder = firmwareSize % readSize;

                                if ((versionMajor < 10) && (versionMinor < 100)) {
                                    if (((firmwareSize + 36) == fileSize) && (firmwareSize > 0) && (firmwareSize <= maxFirmwareSize)) {

                                        // 6. PAYLOAD READ INTO SDRAM
                                        // Copy firmware payload from file to RAM staging area.
                                        for (uint16_t i = 0; i < (readCount); i++) {
                                            f_read(&sd.file, (uint8_t *)(RAM_FIRMWARE_ADDRESS + (i * readSize)), readSize, &sd.bytesread);
                                        }
                                        f_read(&sd.file, (uint8_t *)(RAM_FIRMWARE_ADDRESS + (readCount * readSize)), remainder, &sd.bytesread);

                                        // 7. CRC VALIDATION
                                        // Reject the staged payload before touching flash if its CRC doesn't match the header.
                                        uint32_t crcValue = crc_update(0, (const void *)(RAM_FIRMWARE_ADDRESS), firmwareSize);

                                        if (crcValue == crcRef) {
                                            // Pad the staging buffer with erased-flash bytes up to the next
                                            // 256-bit flashword so the final partial word is deterministic.
                                            uint32_t paddedSize = ((firmwareSize + FLASH_WORD_BYTES - 1) / FLASH_WORD_BYTES) * FLASH_WORD_BYTES;
                                            memset((uint8_t *)(RAM_FIRMWARE_ADDRESS + firmwareSize), 0xFF, paddedSize - firmwareSize);

                                            // 8. FLASH PROGRAM
                                            // Erase target sectors and write staged payload to flash.
                                            lcd.drawSdFirmwareAlert(true);

                                            lcd.setForeColor(GREEN);
                                            lcd.setBackColor(BLACK);
                                            lcd.setFont(FONT_07x09);
                                            lcd.setAlignment(CENTER);
                                            uint16_t xPos = 380;
                                            uint16_t yPos = 412;
                                            lcd.drawHLine(xPos, yPos, 200);
                                            lcd.drawHLine(xPos, yPos + 24, 200);
                                            lcd.drawHalfCircle(xPos - 1, yPos + 12, 12, 1);
                                            lcd.drawHalfCircle(xPos + 200, yPos + 12, 12, 3);

                                            lcd.drawText("UPDATING FIRMWARE", 17, 480, 420);

                                            lcd.setForeColor(GRAY_25);
                                            lcd.drawHLine(380, 365, 200);

                                            lcd.setForeColor(GREEN);
                                            lcd.setBackColor(BLACK);
                                            lcd.setFont(FONT_05x07);
                                            lcd.setAlignment(CENTER);
                                            char textVersion[7];
                                            snprintf(textVersion, sizeof(textVersion), "V%01u.%02u", versionMajor, versionMinor);
                                            lcd.drawText(textVersion, strlen(textVersion), 480, 338);

                                            // From here on the application image is no longer trustworthy
                                            // until the post-write CRC passes.
                                            flashDirty = true;
                                            fwResult = FW_ERROR_FLASH;

                                            if (flash_eraseSector(FLASH_FIRMWARE_ADDRESS, firmwareSize) == HAL_OK) {
                                                HAL_Delay(250);
                                                if (flash_writeData(FLASH_FIRMWARE_ADDRESS, (uint32_t *)(RAM_FIRMWARE_ADDRESS), firmwareSize) == HAL_OK) {

                                                    // 8b. POST-WRITE VERIFICATION
                                                    // Re-read the programmed flash region and recheck CRC before declaring success.
                                                    uint32_t verifyCrc = crc_update(0, (const void *)(FLASH_FIRMWARE_ADDRESS), firmwareSize);

                                                    if (verifyCrc == crcRef) {
                                                        fwResult = FW_OK;
                                                        flashDirty = false;
                                                        lcd.setForeColor(GREEN);
                                                        lcd.setBackColor(BLACK);
                                                        for (uint8_t i = 0; i < 200; i++) {
                                                            lcd.drawPixel(380 + i, 365);
                                                            HAL_Delay(5);
                                                        }
                                                        HAL_Delay(500);
                                                    } else {
                                                        fwResult = FW_ERROR_VERIFY;
                                                    }
                                                }
                                            }
                                        } else {
                                            fwResult = FW_ERROR_CRC;
                                        }
                                    } else {
                                        fwResult = FW_ERROR_SIZE;
                                    }
                                } else {
                                    fwResult = FW_ERROR_VERSION;
                                }
                            } else {
                                fwResult = FW_ERROR_FORMAT;
                            }
                        }

                        // 9. RESULT FEEDBACK UI
                        // Show success or detailed failure message.
                        if (fwResult == FW_OK) {
                            lcd.setForeColor(GREEN);
                            lcd.setBackColor(BLACK);
                            lcd.setFont(FONT_07x09);
                            lcd.setAlignment(CENTER);
                            lcd.drawText(" FIRMWARE UPDATED ", 18, 480, 420);
                        } else {
                            lcd.drawSdFirmwareAlert(false);

                            lcd.setForeColor(RED);
                            lcd.setBackColor(BLACK);
                            lcd.setFont(FONT_07x09);
                            lcd.setAlignment(CENTER);
                            uint16_t xPos = 380;
                            uint16_t yPos = 412;
                            lcd.drawHLine(xPos, yPos, 200);
                            lcd.drawHLine(xPos, yPos + 24, 200);
                            lcd.drawHalfCircle(xPos - 1, yPos + 12, 12, 1);
                            lcd.drawHalfCircle(xPos + 200, yPos + 12, 12, 3);
                            lcd.drawText("  FIRMWARE ERROR  ", 18, 480, 420);

                            lcd.setFont(FONT_05x07);
                            switch (fwResult) {
                            case FW_ERROR:
                                lcd.drawText("FORMAT-CHECK", 12, 480, 360);
                                break;

                            case FW_ERROR_FORMAT:
                                lcd.drawText("FORMAT-CHECK", 12, 480, 360);
                                break;

                            case FW_ERROR_VERSION:
                                lcd.drawText("VERSION-CHECK", 13, 480, 360);
                                break;

                            case FW_ERROR_SIZE:
                                lcd.drawText("SIZE-CHECK", 10, 480, 360);
                                break;

                            case FW_ERROR_CRC:
                                lcd.drawText("CRC-CHECK", 9, 480, 360);
                                break;

                            case FW_ERROR_VERIFY:
                                lcd.drawText("VERIFY-CHECK", 12, 480, 360);
                                break;

                            case FW_ERROR_FLASH:
                                lcd.drawText("FLASH-CHECK", 11, 480, 360);
                                break;

                            default:
                                break;
                            }
                        }
                    }

                    // 10. FILE FINALIZATION
                    // Move successful package into archive folder. A package that failed validation is
                    // deleted, but one that failed while programming flash is kept so the next boot
                    // retries it (the application is already erased at that point).
                    f_close(&sd.file);
                    if (fwResult == FW_OK) {
                        sd_deleteDirectory("System/Firmware");
                        sd_createDirectory("System/Firmware");
                        char fName[sizeof(sd.fileInfo.fname) + 16];
                        strcpy(fName, "System/Firmware/");
                        strcat(fName, sd.fileInfo.fname);
                        f_rename(sd.fileInfo.fname, fName);
                    } else if (!flashDirty) {
                        f_unlink(sd.fileInfo.fname);
                    }
                    HAL_Delay(2500);
                    lcd.clearSdFirmwareAlert();
                }
            }
        }
    }
}

bool Controller::isApplicationValid() {
    /// @brief Sanity-checks the application vector table before jumping to it.

    // 1. VECTOR TABLE READ
    // Word 0 is the initial stack pointer, word 1 the reset handler.
    uint32_t stackPointer = *(__IO uint32_t *)(FLASH_FIRMWARE_ADDRESS);
    uint32_t resetHandler = *(__IO uint32_t *)(FLASH_FIRMWARE_ADDRESS + 4);

    // 2. STACK POINTER CHECK
    // Must be 8-byte aligned and inside DTCM (128 KB) or AXI SRAM / RAM_D1 (320 KB).
    // Erased flash reads 0xFFFFFFFF and fails here.
    bool stackOk = ((stackPointer & 0x7) == 0) &&
                   (((stackPointer >= 0x20000000) && (stackPointer <= 0x20020000)) ||
                    ((stackPointer >= 0x24000000) && (stackPointer <= 0x24050000)));

    // 3. RESET HANDLER CHECK
    // Must be a Thumb address (bit 0 set) located inside the application flash region.
    uint32_t resetAddress = resetHandler & ~1UL;
    bool resetOk = (resetHandler & 0x1) &&
                   (resetAddress >= FLASH_FIRMWARE_ADDRESS + 8) &&
                   (resetAddress < FLASH_FIRMWARE_END_ADDRESS);

    return stackOk && resetOk;
}

void Controller::haltNoApplication() {
    /// @brief Shows a permanent error and stays in the bootloader when no usable application exists.

    // 1. DISPLAY BRING-UP
    // The LCD is only initialized when a firmware package was found, so do it here if needed.
    if (!lcdActive) {
        lcd.initialize();
        lcd.displayOn();
        lcdActive = true;
    }

    // 2. ERROR SCREEN
    lcd.drawSdFirmwareAlert(false);

    lcd.setForeColor(RED);
    lcd.setBackColor(BLACK);
    lcd.setFont(FONT_07x09);
    lcd.setAlignment(CENTER);
    uint16_t xPos = 380;
    uint16_t yPos = 412;
    lcd.drawHLine(xPos, yPos, 200);
    lcd.drawHLine(xPos, yPos + 24, 200);
    lcd.drawHalfCircle(xPos - 1, yPos + 12, 12, 1);
    lcd.drawHalfCircle(xPos + 200, yPos + 12, 12, 3);
    lcd.drawText("  FIRMWARE ERROR  ", 18, 480, 420);

    lcd.setFont(FONT_05x07);
    lcd.drawText("NO VALID APPLICATION", 20, 480, 360);
    lcd.drawText("COPY FIRMWARE TO SD AND RESTART", 31, 480, 338);

    // 3. HALT
    // Recovery path: put a firmware package on the SD card and power-cycle.
    while (true) {
    }
}

void Controller::startApplication() {
    /// @brief Transfers execution to the application vector table entry point.

    void (*SysMemBootJump)(void);
    SysMemBootJump = (void (*)(void))(*((uint32_t *)(FLASH_FIRMWARE_ADDRESS + 4)));

    // 1. INTERRUPT DISABLE
    // Prevent any IRQ from firing between MSP update and the jump.
    // SysTick is stopped explicitly to avoid a tick interrupt during the transition.
    __disable_irq();
    SysTick->CTRL = 0;

    // 2. STACK POINTER UPDATE + JUMP
    __set_MSP(*(uint32_t *)FLASH_FIRMWARE_ADDRESS);
    SysMemBootJump();
}

////////////////////////////////////////////////////////////////////////////////
/* Crc Functions ---------------------------------------------------------------*/
////////////////////////////////////////////////////////////////////////////////

uint32_t Controller::crc_update(uint32_t initial, const void *buf, uint32_t len) {
    // 1. CRC-32 (zlib/ISO-3309 polynomial) over an arbitrary buffer.
    // Pass initial=0 for a standalone checksum, or a prior return value to continue across chunks.
    uint32_t c = initial ^ 0xFFFFFFFF;
    const uint8_t *u = static_cast<const uint8_t *>(buf);
    for (uint32_t i = 0; i < len; ++i) {
        c = kCrcTable[(c ^ u[i]) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFF;
}

////////////////////////////////////////////////////////////////////////////////
/* Flash Functions -----------------------------------------------------------*/
////////////////////////////////////////////////////////////////////////////////

uint32_t Controller::flash_getSector(uint32_t address) {
    // 1. ADDRESS-TO-SECTOR MAPPING
    // Resolve STM32H7 flash sector from absolute address. Out-of-range addresses return
    // FLASH_SECTOR_INVALID (previously 0, which silently aliased the bootloader's own sector).
    uint32_t sector = FLASH_SECTOR_INVALID;

    if ((address >= 0x08000000) && (address < 0x08020000)) {
        sector = FLASH_SECTOR_0;
    }

    else if ((address >= 0x08020000) && (address < 0x08040000)) {
        sector = FLASH_SECTOR_1;
    }

    else if ((address >= 0x08040000) && (address < 0x08060000)) {
        sector = FLASH_SECTOR_2;
    }

    else if ((address >= 0x08060000) && (address < 0x08080000)) {
        sector = FLASH_SECTOR_3;
    }

    else if ((address >= 0x08080000) && (address < 0x080A0000)) {
        sector = FLASH_SECTOR_4;
    }

    else if ((address >= 0x080A0000) && (address < 0x080C0000)) {
        sector = FLASH_SECTOR_5;
    }

    else if ((address >= 0x080C0000) && (address < 0x080E0000)) {
        sector = FLASH_SECTOR_6;
    }

    else if ((address >= 0x080E0000) && (address < 0x08100000)) {
        sector = FLASH_SECTOR_7;
    }

    return sector;
}

uint32_t Controller::flash_eraseSector(uint32_t startSectorAddress, uint32_t byteSize) {
    // 1. ERASE BOUNDS
    // Erase every sector touched by [start, start + byteSize). The end is computed from the
    // last byte (inclusive) so an image ending exactly on a sector boundary does not spill
    // into the next sector or past the end of flash. Never touch sector 0 (bootloader).
    // Returns 0 on success, non-zero on failure.
    if (byteSize == 0) {
        return 1;
    }

    uint32_t startSector = flash_getSector(startSectorAddress);
    uint32_t endSector = flash_getSector(startSectorAddress + byteSize - 1);

    if ((startSector == FLASH_SECTOR_INVALID) || (endSector == FLASH_SECTOR_INVALID) ||
        (startSector == FLASH_SECTOR_0) || (endSector < startSector)) {
        return 1;
    }

    // 2. ERASE DESCRIPTOR SETUP
    // Configure HAL erase struct.
    static FLASH_EraseInitTypeDef EraseInitStruct;
    uint32_t SECTORError;

    // 3. FLASH UNLOCK + ERASE
    // Perform sector erase transaction.
    HAL_FLASH_Unlock();

    EraseInitStruct.TypeErase = FLASH_TYPEERASE_SECTORS;
    EraseInitStruct.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    EraseInitStruct.Banks = FLASH_BANK_1;
    EraseInitStruct.Sector = startSector;
    EraseInitStruct.NbSectors = (endSector - startSector) + 1;

    if (HAL_FLASHEx_Erase(&EraseInitStruct, &SECTORError) != HAL_OK) {
        HAL_FLASH_Lock();
        // HAL_FLASH_GetError() can be 0 on a timeout; never report that as success.
        uint32_t error = HAL_FLASH_GetError();
        return (error != 0) ? error : 1;
    }

    // 4. FLASH RELOCK
    // Re-lock flash after erase operation.
    HAL_FLASH_Lock();
    return 0;
}

uint32_t Controller::flash_writeData(uint32_t startSectorAddress, uint32_t *data, uint32_t byteSize) {
    // 1. PROGRAM STRIDE SETUP
    // 256 bits for STM32H72x/3X devices (8x 32-bit words). The byte count is rounded up to
    // whole flashwords so a size that is not a multiple of 4 or 32 still gets its tail
    // written. The caller must provide a source buffer padded to that length.
    // Returns 0 on success, non-zero on failure.
    const uint32_t wordsPerFlashWord = FLASH_WORD_BYTES / 4;
    uint32_t flashWordCount = (byteSize + FLASH_WORD_BYTES - 1) / FLASH_WORD_BYTES;

    if ((byteSize == 0) || (startSectorAddress < FLASH_FIRMWARE_ADDRESS) ||
        (startSectorAddress + (flashWordCount * FLASH_WORD_BYTES) > FLASH_FIRMWARE_END_ADDRESS)) {
        return 1;
    }

    // 2. FLASHWORD PROGRAM LOOP
    // Program payload in flashword chunks.
    HAL_FLASH_Unlock();

    for (uint32_t i = 0; i < flashWordCount; i++) {
        uint32_t address = startSectorAddress + (i * FLASH_WORD_BYTES);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, address, (uint32_t)&data[i * wordsPerFlashWord]) != HAL_OK) {
            HAL_FLASH_Lock();
            uint32_t error = HAL_FLASH_GetError();
            return (error != 0) ? error : 1;
        }
    }

    // 3. FLASH RELOCK
    // Re-lock flash after write operation.
    HAL_FLASH_Lock();
    return 0;
}

void Controller::flash_readData(uint32_t StartSectorAddress, uint32_t *data, uint32_t wordSize) {
    // 1. LINEAR READBACK LOOP
    // Copy word-by-word from flash to destination buffer.
    for (uint32_t i = 0; i < wordSize; i++) {
        *data++ = *(__IO uint32_t *)StartSectorAddress;
        StartSectorAddress += 4;
    }
}

////////////////////////////////////////////////////////////////////////////////
/* Dac Functions -------------------------------------------------------------*/
////////////////////////////////////////////////////////////////////////////////

void Controller::dac_initialize() {
    // 1. DAC INITIALIZATION
    // Initialize audio output peripheral.
    dac.initialize();
}

////////////////////////////////////////////////////////////////////////////////
/* Sd Functions --------------------------------------------------------------*/
////////////////////////////////////////////////////////////////////////////////

void Controller::sd_initialize() {
    // 1. SDMMC BOOTSTRAP
    // Initialize SDMMC peripheral only when card-detect is valid.
    if (sd_detect() == SD_OK) {
        MX_SDMMC2_SD_Init();
    }
}

SdResult Controller::sd_detect() {
    // 1. CARD-DETECT PIN READ
    // Translate hardware detect level to SdResult.
    SdResult sdResult;
    (HAL_GPIO_ReadPin(SDMMC2_DETECT_GPIO_Port, SDMMC2_DETECT_Pin)) ? sdResult = SD_ERROR : sdResult = SD_OK;
    return sdResult;
}

SdResult Controller::sd_mount() {
    // 1. FILESYSTEM MOUNT
    // Mount FATFS at SDPath.
    SdResult sdResult;
    (f_mount(&sd.fs, SDPath, 0) == FR_OK) ? sdResult = SD_OK : sdResult = SD_ERROR;
    return sdResult;
}

SdResult Controller::sd_unmount() {
    // 1. FILESYSTEM UNMOUNT
    // Unmount FATFS from SDPath.
    SdResult sdResult;
    (f_mount(0, SDPath, 0) == FR_OK) ? sdResult = SD_OK : sdResult = SD_ERROR;
    return sdResult;
}

SdResult Controller::sd_getSpace() {
    // 1. CAPACITY QUERY
    // Compute total/free/used size from FAT cluster data.
    SdResult sdResult;
    FATFS *fs_ptr = &sd.fs;
    uint32_t freeCluster;
    if (f_getfree(SDPath, (DWORD *)&freeCluster, &fs_ptr) == FR_OK) {
        uint32_t totalBlocks = (sd.fs.n_fatent - 2) * sd.fs.csize;
        uint32_t freeBlocks = freeCluster * sd.fs.csize;
        sd.totalSpace = totalBlocks / 2000;
        sd.freeSpace = freeBlocks / 2000;
        sd.usedSpace = sd.totalSpace - sd.freeSpace;
        sdResult = SD_OK;
    } else {
        sd.totalSpace = 0;
        sd.freeSpace = 0;
        sd.usedSpace = 0;
        sdResult = SD_ERROR;
    }
    return sdResult;
}

SdResult Controller::sd_checkFileExist(const char *fileAddress) {
    // 1. FILE TYPE CHECK
    // Verify path exists and is not a directory.
    SdResult sdResult;
    if ((f_stat(fileAddress, &sd.fileInfo) == FR_OK) && ((sd.fileInfo.fattrib & AM_DIR) == false)) {
        sdResult = SD_OK;
    } else {
        sdResult = SD_ERROR;
    }
    return sdResult;
}

SdResult Controller::sd_checkFolderExist(const char *folderAddress) {
    // 1. DIRECTORY TYPE CHECK
    // Verify path exists and is a directory.
    SdResult sdResult;
    if ((f_stat(folderAddress, &sd.fileInfo) == FR_OK) && (sd.fileInfo.fattrib & AM_DIR)) {
        sdResult = SD_OK;
    } else {
        sdResult = SD_ERROR;
    }
    return sdResult;
}

FRESULT Controller::sd_createDirectory(const char *path) {
    // 1. MKDIR WRAPPER
    // Create directory at the given path.
    FRESULT res;
    res = f_mkdir(path);
    return res;
}

FRESULT Controller::sd_deleteDirectory(const char *path) {
    // 1. RECURSIVE DELETE WALK
    // Delete all files/subdirs, then remove the root directory.
    FRESULT res;
    DIR dir;
    FILINFO fileInfo;
    char file[256] = "";
    bool listFile = true;

    res = f_opendir(&dir, path);
    if (res != FR_OK) {
        return res;
    }

    while (listFile) {
        res = f_readdir(&dir, &fileInfo);
        if ((res == FR_OK) && (fileInfo.fname[0] != 0)) {
            // Long file names can exceed the buffer; skip such entries instead of overflowing.
            int len = snprintf(file, sizeof(file), "%s/%s", path, fileInfo.fname);
            if ((len < 0) || ((size_t)len >= sizeof(file))) {
                continue;
            }
            (fileInfo.fattrib & AM_DIR) ? sd_deleteDirectory(file) : f_unlink(file);
        } else {
            listFile = false;
        }
    }

    f_closedir(&dir);
    f_unlink(path);
    return res;
}

////////////////////////////////////////////////////////////////////////////////
/* Lcd Functions -------------------------------------------------------------*/
////////////////////////////////////////////////////////////////////////////////

void Controller::lcd_initialize() {
    // 1. LCD CORE INIT
    // Initialize display controller and bus state.
    lcd.initialize();
}