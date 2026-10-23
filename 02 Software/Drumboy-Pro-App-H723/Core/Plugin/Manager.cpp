#include "Manager.h"

#ifdef DRUMBOY_PLUGINS

#include <strings.h>

#include "Branding.h"
#include "Controller.h"
#include "Plugin.h"

// clang-format off

/* Settings ---------------------------------------------------------*/

const uint8_t  kManagerRows                         = 8;                // Item lanes (lane 0 = title, lane 9 = status)
const uint8_t  kManagerFirmwareMax                  = 12;
const char     kManagerFirmwareFolder[]             = "Firmware";
const char     kManagerFirmwarePrefix[]             = "Drumboy_Pro_Fw";    // Bootloader looks for Drumboy_Pro_Fw?*.bin
const char     kManagerFirmwareHeader[]             = "RW_DRUMBOY_PRO_FIRMWARE ";
const uint32_t kManagerFirmwareMaxSize              = 917504;           // Bootloader limit (sectors 1-7)
const uint16_t kManagerCopyChunk                    = 2048;

const uint8_t  kKeypadLeft                          = 1;
const uint8_t  kKeypadRight                         = 2;

// clang-format on

enum ManagerPage : uint8_t { PAGE_PLUGINS, PAGE_FIRMWARE, PAGE_COUNT };

struct FirmwareEntry {
    char name[33];
    uint8_t major;
    uint8_t minor;
    uint32_t size;
};

/* State ------------------------------------------------------------*/

static bool open = false;
static bool redraw = false;
static bool restorePending = false;
static bool configChanged = false;
static bool wasFileMenu = false;                                        // File menu was already open before this pass
static ManagerPage page = PAGE_PLUGINS;
static uint8_t cursor[PAGE_COUNT] = {0, 0};
static int8_t confirmIndex = -1;

static FirmwareEntry firmware[kManagerFirmwareMax];
static uint8_t firmwareCount = 0;

static char status[112] = "";
static bool statusError = false;

/* Helpers ----------------------------------------------------------*/

static uint16_t laneY(uint8_t lane) {
    return kImageLayerY[lane] + kSongOffsetY;
}

static void drawLane(Lcd& lcd, uint8_t lane, const char* text, RGB16Color color, LcdFont font) {
    /// @brief Clears one song lane (same rectangle as lcd_clearSong) and writes a line of text into it.

    lcd.setBackColor(BLACK);
    lcd.clearRect(kSongX, laneY(lane), kSongWidth + 1, kSongHeight + 1);
    lcd.setFont(font);
    lcd.setAlignment(LEFT);
    lcd.setForeColor(color);

    // Clip to the lane so nothing spills into the stock UI beside it
    uint16_t glyphWidth = (font == FONT_05x07) ? 5 : 7;
    size_t maxChars = (kSongWidth - 4) / (glyphWidth + lcd.getSpacing());
    size_t len = strlen(text);
    if (len > maxChars) len = maxChars;
    if (len > 0) lcd.drawText(text, (uint16_t)len, kSongX + 2, laneY(lane) + ((font == FONT_05x07) ? 1 : 0));
}

static void setStatus(const char* text, bool error) {
    snprintf(status, sizeof(status), "%s", text);
    statusError = error;
    redraw = true;
}

static uint8_t itemCount() {
    return (page == PAGE_PLUGINS) ? pluginHost_getCount() : firmwareCount;
}

static uint32_t crc32Update(uint32_t crc, const uint8_t* data, uint32_t len) {
    /// @brief Standard reflected CRC-32 (zlib), same as the bootloader and make_firmware_package.py.

    crc = ~crc;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320 & (0u - (crc & 1)));
    }
    return ~crc;
}

/* Firmware Library -------------------------------------------------*/

static bool readHeader(FIL& file, uint8_t& major, uint8_t& minor, uint32_t& size, uint32_t& crc) {
    /// @brief Reads and validates the 36-byte package header at the current position.

    static uint8_t header[36] __attribute__((aligned(32)));
    UINT bytesRead = 0;
    if ((f_read(&file, header, sizeof(header), &bytesRead) != FR_OK) || (bytesRead != sizeof(header))) return false;
    if (memcmp(header, kManagerFirmwareHeader, 24) != 0) return false;

    uint16_t ma = header[24] | (header[25] << 8);
    uint16_t mi = header[26] | (header[27] << 8);
    size = header[28] | (header[29] << 8) | (header[30] << 16) | ((uint32_t)header[31] << 24);
    crc = header[32] | (header[33] << 8) | (header[34] << 16) | ((uint32_t)header[35] << 24);
    if ((ma >= 10) || (mi >= 100) || (size == 0) || (size >= kManagerFirmwareMaxSize)) return false;
    if ((size + sizeof(header)) != f_size(&file)) return false;

    major = (uint8_t)ma;
    minor = (uint8_t)mi;
    return true;
}

static void scanFirmware(Controller& controller) {
    /// @brief Lists valid update packages in the Firmware folder, sorted by name.

    static DIR dir;
    static FILINFO info;
    FIL& file = controller.sd.file;
    firmwareCount = 0;

    controller.keyboard_disable();
    if (f_opendir(&dir, kManagerFirmwareFolder) == FR_OK) {
        while ((firmwareCount < kManagerFirmwareMax) && (f_readdir(&dir, &info) == FR_OK) && (info.fname[0] != '\0')) {
            size_t len = strlen(info.fname);
            if ((info.fattrib & (AM_DIR | AM_HID)) || (info.fname[0] == '.') || (len <= 4) || (len >= sizeof(firmware[0].name))) continue;
            if (strcasecmp(&info.fname[len - 4], ".bin") != 0) continue;

            char path[48];
            snprintf(path, sizeof(path), "%s/%s", kManagerFirmwareFolder, info.fname);
            if (f_open(&file, path, FA_READ) != FR_OK) continue;

            FirmwareEntry& entry = firmware[firmwareCount];
            uint32_t crc;
            if (readHeader(file, entry.major, entry.minor, entry.size, crc)) {
                strcpy(entry.name, info.fname);
                firmwareCount++;
            }
            f_close(&file);
        }
        f_closedir(&dir);
    }
    controller.keyboard_enable();

    // Insertion sort by name (few entries)
    for (uint8_t i = 1; i < firmwareCount; i++) {
        FirmwareEntry entry = firmware[i];
        int8_t j = i - 1;
        while ((j >= 0) && (strcasecmp(firmware[j].name, entry.name) > 0)) {
            firmware[j + 1] = firmware[j];
            j--;
        }
        firmware[j + 1] = entry;
    }
}

static void showProgress(Controller& controller, const char* text, bool error) {
    // Draws the status lane immediately (the copy blocks the main loop).
    drawLane(controller.lcd, kManagerRows + 1, text, error ? RED : YELLOW, FONT_05x07);
}

static void installFirmware(Controller& controller, const FirmwareEntry& entry) {
    /// @brief Copies Firmware/<name> to the SD root (verifying its CRC on the way) and restarts;
    /// the bootloader then installs it. The library copy stays in Firmware/.

    FIL& source = controller.sd.file;
    FIL target;  // On the stack (RAM_D1, reachable by SDMMC DMA); used only during the copy
    static uint8_t chunk[kManagerCopyChunk] __attribute__((aligned(32)));

    char sourcePath[48];
    char targetPath[40];
    snprintf(sourcePath, sizeof(sourcePath), "%s/%s", kManagerFirmwareFolder, entry.name);
    if (strncmp(entry.name, kManagerFirmwarePrefix, strlen(kManagerFirmwarePrefix)) == 0) {
        snprintf(targetPath, sizeof(targetPath), "%s", entry.name);
    } else {
        snprintf(targetPath, sizeof(targetPath), "%s_library.bin", kManagerFirmwarePrefix);
    }

    // 1. SAVE PENDING PLUGIN CHANGES FIRST (the restart would lose them)
    controller.keyboard_disable();
    if (configChanged) {
        pluginHost_saveConfig(source);
        configChanged = false;
    }

    // 2. OPEN + VALIDATE SOURCE
    bool ok = false;
    uint8_t major = 0, minor = 0;
    uint32_t size = 0, crcRef = 0;
    showProgress(controller, "CHECKING...", false);
    if ((f_open(&source, sourcePath, FA_READ) == FR_OK) && readHeader(source, major, minor, size, crcRef) && (f_lseek(&source, 0) == FR_OK)) {
        // 3. COPY (header + payload), CRC over the payload
        if (f_open(&target, targetPath, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
            uint32_t total = size + 36;
            uint32_t done = 0;
            uint32_t crc = 0;
            bool io = true;
            while (io && (done < total)) {
                UINT toRead = (total - done < kManagerCopyChunk) ? (UINT)(total - done) : kManagerCopyChunk;
                UINT bytesRead = 0, bytesWritten = 0;
                io = (f_read(&source, chunk, toRead, &bytesRead) == FR_OK) && (bytesRead == toRead) &&
                     (f_write(&target, chunk, toRead, &bytesWritten) == FR_OK) && (bytesWritten == toRead);
                if (io) {
                    uint32_t skip = (done < 36) ? 36 - done : 0;  // Header is not part of the CRC
                    if (skip < toRead) crc = crc32Update(crc, &chunk[skip], toRead - skip);
                    done += toRead;
                    if ((done % (64 * 1024)) < kManagerCopyChunk) {
                        char text[48];
                        snprintf(text, sizeof(text), "COPYING %lu%%", (unsigned long)(done * 100 / total));
                        showProgress(controller, text, false);
                    }
                }
            }
            ok = io && (f_close(&target) == FR_OK) && (crc == crcRef);
            if (!ok) f_unlink(targetPath);
        }
        f_close(&source);
    }

    // 4. RESTART INTO THE BOOTLOADER, OR REPORT
    if (ok) {
        char text[96];
        snprintf(text, sizeof(text), "INSTALLING V%u.%02u FROM %s - RESTARTING...", major, minor, entry.name);
        showProgress(controller, text, false);
        HAL_Delay(800);
        NVIC_SystemReset();
    }
    controller.keyboard_enable();
    setStatus("INSTALL FAILED: FILE UNREADABLE OR CORRUPT (CRC). NOTHING WAS CHANGED.", true);
}

/* Drawing ----------------------------------------------------------*/

static void drawPage(Controller& controller) {
    Lcd& lcd = controller.lcd;
    char text[112];

    // 1. TITLE (lane 0)
    snprintf(text, sizeof(text), "%s  %s     UP/DOWN MOVE  CENTER SELECT  LEFT/RIGHT PAGE  FILE CLOSE",
             (page == PAGE_PLUGINS) ? "[PLUGINS]" : " PLUGINS ", (page == PAGE_FIRMWARE) ? "[FIRMWARE]" : " FIRMWARE ");
    drawLane(lcd, 0, text, CYAN, FONT_05x07);

    // 2. ITEMS (lanes 1-8), scrolled so the cursor stays visible
    uint8_t count = itemCount();
    uint8_t first = (cursor[page] >= kManagerRows) ? cursor[page] - kManagerRows + 1 : 0;
    for (uint8_t row = 0; row < kManagerRows; row++) {
        uint8_t i = first + row;
        RGB16Color color = GRAY;
        text[0] = '\0';

        if ((count == 0) && (row == 0)) {
            snprintf(text, sizeof(text), (page == PAGE_PLUGINS) ? "  NO PLUGINS" : "  NO FILES - PUT UPDATE FILES IN A FIRMWARE FOLDER ON THE SD CARD");
        } else if (i < count) {
            char mark = (i == cursor[page]) ? '>' : ' ';
            if (page == PAGE_PLUGINS) {
                Plugin* plugin = pluginHost_getPlugin(i);
                bool on = pluginHost_isConfigured(i);
                bool running = pluginHost_isRunning(i);
                const char* state = running ? (on ? "RUNNING" : "RUNNING - OFF AFTER RESTART") : (on ? "ON AFTER RESTART" : "");
                snprintf(text, sizeof(text), "%c %-3s  %-20s %-9s %s", mark, on ? "ON" : "OFF", plugin->id(), plugin->kind(), state);
                color = on ? GREEN : GRAY;
            } else {
                const FirmwareEntry& fw = firmware[i];
                const char* action = (confirmIndex == (int8_t)i) ? "CENTER AGAIN: INSTALL" : "";
                snprintf(text, sizeof(text), "%c %-30s V%u.%02u %4luKB %s", mark, fw.name, fw.major, fw.minor, (unsigned long)(fw.size / 1024), action);
                color = (confirmIndex == (int8_t)i) ? YELLOW : WHITE;
            }
            if (i == cursor[page]) color = (color == GRAY) ? WHITE : color;
        }
        drawLane(lcd, row + 1, text, color, FONT_07x09);
    }

    // 3. STATUS (lane 9)
    if (status[0] == '\0') {
        snprintf(text, sizeof(text), "RUNNING CYBERYAK %s   %s", branding_getVersionString(),
                 (page == PAGE_PLUGINS) ? "CHANGES SAVE ON CLOSE, APPLY AFTER RESTART" : "FILES FROM THE FIRMWARE FOLDER ON THE SD CARD");
        drawLane(lcd, kManagerRows + 1, text, GRAY, FONT_05x07);
    } else {
        drawLane(lcd, kManagerRows + 1, status, statusError ? RED : YELLOW, FONT_05x07);
    }
}

static void restoreLanes(Controller& controller) {
    /// @brief Gives the song lanes back to the stock UI.

    uint8_t bank = controller.getActiveSongBank();
    for (uint8_t lane = 0; lane < kLayerLibrarySize; lane++) controller.lcd_drawSong(lane, bank);
}

/* Open / Close -----------------------------------------------------*/

static void openManager(Controller& controller) {
    open = true;
    redraw = true;
    restorePending = false;
    confirmIndex = -1;
    status[0] = '\0';
    scanFirmware(controller);
    if (cursor[PAGE_FIRMWARE] >= firmwareCount) cursor[PAGE_FIRMWARE] = 0;
}

static void closeManager(Controller& controller) {
    if (configChanged) {
        controller.keyboard_disable();
        bool saved = pluginHost_saveConfig(controller.sd.file);
        controller.keyboard_enable();
        pluginHost_message(saved ? "PLUGINS SAVED - RESTART THE DRUMBOY TO APPLY" : "COULD NOT SAVE PLUGINS/PLUGINS.TXT", !saved);
        if (saved) configChanged = false;
    }
    open = false;
    restorePending = true;
}

/* Public -----------------------------------------------------------*/

bool manager_isOpen() {
    return open;
}

bool manager_needsFrame() {
    return open || restorePending;
}

void manager_endUpdate(Controller& controller) {
    wasFileMenu = (controller.getMenu() == FILE_MENU);
}

bool manager_handleEvent(const PluginEvent& e, Controller& controller) {
    /// @brief Opens on FILE (pressed while the File menu was already open); while open, uses all keys and knobs.

    // 1. CLOSED: WATCH FOR THE OPEN GESTURE
    if (!open) {
        if ((e.type == PLUGIN_EVENT_KEY) && (e.a == kKeypadRight) && (e.b == KEY_FILE) && e.c && wasFileMenu &&
            (controller.getMenu() == FILE_MENU)) {
            if (controller.isRecording()) {
                pluginHost_message("STOP RECORDING TO OPEN THE PLUGIN & FIRMWARE MANAGER", true);
            } else {
                openManager(controller);
            }
            return true;
        }
        return false;
    }

    // 2. OPEN: KNOBS MOVE THE CURSOR
    if (e.type == PLUGIN_EVENT_ENCODER) {
        if (e.a == 0) {
            int8_t delta = (int8_t)e.b;
            uint8_t count = itemCount();
            if (count > 0) {
                int16_t next = (int16_t)cursor[page] + delta;
                cursor[page] = (uint8_t)((next < 0) ? 0 : (next >= count) ? count - 1 : next);
                confirmIndex = -1;
                redraw = true;
            }
        }
        return true;
    }
    if (e.type != PLUGIN_EVENT_KEY) return false;  // Steps, notes and MIDI still reach plugins
    if (!e.c) return true;                          // Releases are swallowed

    // 3. OPEN: KEYS
    uint8_t count = itemCount();
    if ((e.a == kKeypadRight) && (e.b == KEY_FILE)) {
        closeManager(controller);
    } else if (e.a == kKeypadLeft) {
        switch (e.b) {
            case KEY_UP:
                if (cursor[page] > 0) cursor[page]--;
                confirmIndex = -1;
                break;

            case KEY_DOWN:
                if (cursor[page] + 1 < count) cursor[page]++;
                confirmIndex = -1;
                break;

            case KEY_LEFT:
            case KEY_RIGHT:
                page = (page == PAGE_PLUGINS) ? PAGE_FIRMWARE : PAGE_PLUGINS;
                confirmIndex = -1;
                status[0] = '\0';
                break;

            case KEY_CENTER:
                if (count == 0) break;
                if (page == PAGE_PLUGINS) {
                    uint8_t i = cursor[page];
                    pluginHost_setConfigured(i, !pluginHost_isConfigured(i));
                    configChanged = true;
                } else if (confirmIndex == (int8_t)cursor[page]) {
                    installFirmware(controller, firmware[cursor[page]]);  // Restarts on success
                    confirmIndex = -1;
                } else {
                    confirmIndex = (int8_t)cursor[page];
                }
                break;

            default:
                break;
        }
    }
    redraw = true;
    return true;
}

void manager_frame(Controller& controller) {
    if (open && redraw) {
        redraw = false;
        drawPage(controller);
    } else if (restorePending) {
        restorePending = false;
        restoreLanes(controller);
    }
}

#endif
