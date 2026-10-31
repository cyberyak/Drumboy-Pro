#ifndef __GLOBAL_H
#define __GLOBAL_H

#include <cmath>

#include "fatfs.h"
#include "main.h"
#include "sdmmc.h"
#include "string.h"

// clang-format off

////////////////////////////////////////////////////////////////////////////////
/* Application Version -------------------------------------------------------*/
////////////////////////////////////////////////////////////////////////////////

const uint8_t kVersionMajor                         = 0x01;
const uint8_t kVersionMinor                         = 0x01;

////////////////////////////////////////////////////////////////////////////////
/* Application Address -------------------------------------------------------*/
////////////////////////////////////////////////////////////////////////////////

#define RAM_FIRMWARE_ADDRESS                        0xC0000000
#define FLASH_FIRMWARE_ADDRESS                      0x08020000
#define FLASH_FIRMWARE_END_ADDRESS                  0x08100000  // exclusive; app owns sectors 1-7 (896 KB)
#define FLASH_SECTOR_INVALID                        0xFFFFFFFF
#define FLASH_WORD_BYTES                            32          // STM32H72x/73x program unit (256 bits)

////////////////////////////////////////////////////////////////////////////////
/* Stm32 Constants -----------------------------------------------------------*/
////////////////////////////////////////////////////////////////////////////////

#define STM32_UNIQUE_ID                             ((uint32_t*)0x1FF1E800)

// (*STM32_UNIQUE_ID) == 2424897;
// (*STM32_UNIQUE_ID) == 2424884;

////////////////////////////////////////////////////////////////////////////////
/* Global Functions ----------------------------------------------------------*/
////////////////////////////////////////////////////////////////////////////////

#if defined(__GNUC__)
#define ALWAYSINLINE                                __attribute__((always_inline)) inline
#else
#define ALWAYSINLINE __forceinline
#endif

#define AUDIO_SAMPLE_RATE                           44100.0f
#define INT24_MAX_FLOAT                             8388607.0f
#define INT32_MAX_FLOAT                             2147483647.0f

#define I2S_BLOCK_SIZE                              32                          // 0.6ms latency
#define I2S_CHANNEL_COUNT                           2                           // stereo

// Total Size                                       = 32 (samples) * 2 (channels) * 2 (ping-pong halves) = 128
#define I2S_TOTAL_BUFFER_SIZE                       (I2S_BLOCK_SIZE * I2S_CHANNEL_COUNT * 2)
#define NORMALIZE_FACTOR_16BIT                      0.00003051757f

const float kInvSampleRate                          = 1.0f / AUDIO_SAMPLE_RATE;

ALWAYSINLINE float amplitudeTodB(float amplitude) { return 20.0f * log10(amplitude); }
ALWAYSINLINE float dBToAmplitude(float dB) { return pow(10.0f, dB / 20.0f); }


template <typename T, int N>
int size(T (&a)[N]) {
    return N - 1;
}

template <typename T>
void swap(T &a, T &b) {
    T temp = a;
    a = b;
    b = temp;
}

const int32_t kAudioSampleRate                      = 44100;

#define LED0_ON                                     HAL_GPIO_WritePin(LED0_GPIO_Port, LED0_Pin, GPIO_PIN_SET)
#define LED1_ON                                     HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET)
#define LED2_ON                                     HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_SET)

#define LED0_OFF                                    HAL_GPIO_WritePin(LED0_GPIO_Port, LED0_Pin, GPIO_PIN_RESET)
#define LED1_OFF                                    HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET)
#define LED2_OFF                                    HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_RESET)

#define LED0_TOGGLE                                 HAL_GPIO_TogglePin(LED0_GPIO_Port, LED0_Pin)
#define LED1_TOGGLE                                 HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin)
#define LED2_TOGGLE                                 HAL_GPIO_TogglePin(LED2_GPIO_Port, LED2_Pin)

////////////////////////////////////////////////////////////////////////////////
/* Delay_us Function ---------------------------------------------------------*/
////////////////////////////////////////////////////////////////////////////////

#define DWT_CTRL                                    (*(volatile uint32_t*)0xE0001000)
#define DWT_CYCCNT                                  (*(volatile uint32_t*)0xE0001004)
#define DWT_LAR                                     (*(volatile uint32_t*)0xE0001FB0) 
#define DEMCR                                       (*(volatile uint32_t*)0xE000EDFC)
#define TRCENA                                      (1 << 24)
#define CYCCNTENA                                   (1 << 0)
#define DWT_LAR_UNLOCK                              (0xC5ACCE55)

static void dwt_initialize() {
    // 1. Enable the Debug Exception and Monitor Control Register (Trace unit)
    DEMCR |= TRCENA;

    // 2. Unlock DWT access (Required for STM32H7 / Cortex-M7)
    DWT_LAR = DWT_LAR_UNLOCK;

    // 3. Reset and start the cycle counter
    DWT_CYCCNT = 0;
    DWT_CTRL |= CYCCNTENA;
}

static inline uint32_t dwt_getMicros() {
    // Standard formula: cycles / (cycles per microsecond)
    return DWT_CYCCNT / (SystemCoreClock / 1000000);
}

static void delay_us(uint32_t us) {
    uint32_t startTick = DWT_CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000);

    // Unsigned subtraction (DWT_CYCCNT - startTick) correctly handles 
    // the overflow/wrap-around as long as the delay is < ~7.8 seconds at 550MHz.
    while ((DWT_CYCCNT - startTick) < ticks);
}


////////////////////////////////////////////////////////////////////////////////
/* Sdram Constants -----------------------------------------------------------*/
////////////////////////////////////////////////////////////////////////////////

#define SDRAM_START_ADDRESS                         ((uint32_t)0xC0000000)
#define SDRAM_END_ADDRESS                           ((uint32_t)0xC0FFFFF0)

#define SDRAM_MEMORY_WIDTH                          FMC_SDRAM_MEM_BUS_WIDTH_16
#define SDCLOCK_PERIOD                              FMC_SDRAM_CLOCK_PERIOD_2

#define SDRAM_TIMEOUT                               ((uint32_t)0xFFFF)
#define SDRAM_MODEREG_BURST_LENGTH_1                ((uint16_t)0x0000)
#define SDRAM_MODEREG_BURST_LENGTH_2                ((uint16_t)0x0001)
#define SDRAM_MODEREG_BURST_LENGTH_4                ((uint16_t)0x0002)
#define SDRAM_MODEREG_BURST_LENGTH_8                ((uint16_t)0x0004)
#define SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL         ((uint16_t)0x0000)
#define SDRAM_MODEREG_BURST_TYPE_INTERLEAVED        ((uint16_t)0x0008)
#define SDRAM_MODEREG_CAS_LATENCY_2                 ((uint16_t)0x0020)
#define SDRAM_MODEREG_CAS_LATENCY_3                 ((uint16_t)0x0030)
#define SDRAM_MODEREG_OPERATING_MODE_STANDARD       ((uint16_t)0x0000)
#define SDRAM_MODEREG_WRITEBURST_MODE_PROGRAMMED    ((uint16_t)0x0000)
#define SDRAM_MODEREG_WRITEBURST_MODE_SINGLE        ((uint16_t)0x0200)

////////////////////////////////////////////////////////////////////////////////
/* Sd Constants --------------------------------------------------------------*/
////////////////////////////////////////////////////////////////////////////////

struct Sd {
    FATFS fs;
    DIR dir;
    FIL file;
    FILINFO fileInfo;
    FRESULT fresult;

    UINT byteswritten;
    UINT bytesread;

    bool detect = false;
    bool ready = false;
    char label[32] = "";
    DWORD serial = 0;
    DWORD serialTemp = 0;

    uint16_t totalSpace = 0;
    uint16_t freeSpace = 0;
    uint16_t usedSpace = 0;

    Sd() {}
    ~Sd() {}
};

/*----------------------------------------------------------------------------*/

typedef enum {
    SD_OK                                           = 0x00,
    SD_ERROR                                        = 0x01,
    SD_ERROR_DETECT                                 = 0x02,
    SD_ERROR_MOUNT                                  = 0x03,
    SD_ERROR_SERIAL                                 = 0x04,
    SD_ERROR_SYSTEMFOLDER                           = 0x05,
    SD_ERROR_SAMPLEFOLDER                           = 0x06,
    SD_ERROR_SYSTEMFILE                             = 0x07
} SdResult;

/*----------------------------------------------------------------------------*/

const char kSdAlertTextInsert[]                     = "INSERT SDCARD";
const char kSdAlertTextFormat[]                     = "SDCARD FORMAT ERROR";
const char kSdAlertTextSerial[]                     = "SDCARD SERIAL ERROR";
const char kSdAlertTextSystemFolder[]               = "SYSTEM FOLDER ERROR";
const char kSdAlertTextSampleFolder[]               = "SAMPLE FOLDER ERROR";
const char kSdAlertTextSystemFile[]                 = "SYSTEM FILE ERROR";
const char kSdAlertTextAnalyze[]                    = "ANALYZING SDCARD";

////////////////////////////////////////////////////////////////////////////////
/* Firmware Constants --------------------------------------------------------*/
////////////////////////////////////////////////////////////////////////////////

typedef enum {
    FW_OK                                           = 0x00,
    FW_ERROR                                        = 0x01,
    FW_ERROR_FORMAT                                 = 0x02,
    FW_ERROR_VERSION                                = 0x03,
    FW_ERROR_SIZE                                   = 0x04,
    FW_ERROR_CRC                                    = 0x05,
    FW_ERROR_VERIFY                                 = 0x06,
    FW_ERROR_FLASH                                  = 0x07,
} FwResult;

////////////////////////////////////////////////////////////////////////////////
/* Crc Constants ---------------------------------------------------------------*/
////////////////////////////////////////////////////////////////////////////////

const uint32_t kCrcTable[256] = {0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7, 0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59, 0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924, 0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433, 0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65, 0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f, 0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1, 0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b, 0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236, 0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d, 0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777, 0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9, 0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d};

////////////////////////////////////////////////////////////////////////////////
/* Lcd Constants -------------------------------------------------------------*/
////////////////////////////////////////////////////////////////////////////////

#define LCD_DATA_GPIO_Port                          GPIOB

#define LCD_CS_HIGH                                 LCD_CS_GPIO_Port->BSRR = LCD_CS_Pin
#define LCD_CS_LOW                                  LCD_CS_GPIO_Port->BSRR = LCD_CS_Pin << 16U

#define LCD_RS_HIGH                                 LCD_RS_GPIO_Port->BSRR = LCD_RS_Pin
#define LCD_RS_LOW                                  LCD_RS_GPIO_Port->BSRR = LCD_RS_Pin << 16U

#define LCD_WR_HIGH                                 LCD_WR_GPIO_Port->BSRR = LCD_WR_Pin
#define LCD_WR_LOW                                  LCD_WR_GPIO_Port->BSRR = LCD_WR_Pin << 16U

#define LCD_RD_HIGH                                 LCD_RD_GPIO_Port->BSRR = LCD_RD_Pin
#define LCD_RD_LOW                                  LCD_RD_GPIO_Port->BSRR = LCD_RD_Pin << 16U

#define LCD_RESET_HIGH                              LCD_RESET_GPIO_Port->BSRR = LCD_RESET_Pin
#define LCD_RESET_LOW                               LCD_RESET_GPIO_Port->BSRR = LCD_RESET_Pin << 16U

#define LCD_BL_HIGH                                 LCD_BL_GPIO_Port->BSRR = LCD_BL_Pin
#define LCD_BL_LOW                                  LCD_BL_GPIO_Port->BSRR = LCD_BL_Pin << 16U

#define LCD_RS_DATA                                 LCD_RS_GPIO_Port->BSRR = LCD_RS_Pin
#define LCD_RS_CMD                                  LCD_RS_GPIO_Port->BSRR = LCD_RS_Pin << 16U

#define LCD_WR_STROBE                               { LCD_WR_LOW; LCD_WR_HIGH; }

/*----------------------------------------------------------------------------*/

#define MADCTL_MY                                   0x80                        // Bottom to top
#define MADCTL_MX                                   0x40                        // Right to left
#define MADCTL_MV                                   0x20                        // Row/Column exchange
#define MADCTL_ML                                   0x10                        // LCD refresh Bottom to top
#define MADCTL_RGB                                  0x00                        // Red-Green-Blue pixel order
#define MADCTL_BGR                                  0x08                        // Blue-Green-Red pixel order
#define MADCTL_MH                                   0x04                        // LCD refresh right to left

/*----------------------------------------------------------------------------*/

const uint16_t kLCD_WIDTH                           = 540;
const uint16_t kLCD_HEIGHT                          = 960;

// R: 5 bit, G: 6 bit, B: 5bit
// https://lvgl.io/tools/imageconverter
// http://www.rinkydinkelectronics.com/calc_rgb565.php

typedef enum {
    RGB16 = 0x00,
    RGB24 = 0x01,
} RGBMode;

typedef uint16_t RGB16Color;
typedef uint32_t RGB24Color;

struct RGB24RawColor {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

/*----------------------------------------------------------------------------*/

const RGB16Color WHITE                              = 0xFFFF;
const RGB16Color BLACK                              = 0x0000;
const RGB16Color GRAY                               = 0x8C51;
const RGB16Color YELLOW                             = 0xFFE0;
const RGB16Color GREEN                              = 0x07E0;
const RGB16Color CYAN                               = 0x07FF;
const RGB16Color MAGENTA                            = 0xF81F;
const RGB16Color BLUE                               = 0x001F;
const RGB16Color RED                                = 0xF800;
const RGB16Color ORANGE                             = 0xFD20;
const RGB16Color PINK                               = 0xF81F;

const RGB16Color GRAY_05                            = 0x0861;
const RGB16Color GRAY_10                            = 0x18C3;
const RGB16Color GRAY_15                            = 0x2124;
const RGB16Color GRAY_20                            = 0x3186;
const RGB16Color GRAY_25                            = 0x4208;
const RGB16Color GRAY_30                            = 0x4A69;
const RGB16Color GRAY_35                            = 0x5ACB;
const RGB16Color GRAY_40                            = 0x632C;
const RGB16Color GRAY_45                            = 0x738E;
const RGB16Color GRAY_50                            = 0x8410;
const RGB16Color GRAY_55                            = 0x8C71;
const RGB16Color GRAY_60                            = 0x9CD3;
const RGB16Color GRAY_65                            = 0xA534;
const RGB16Color GRAY_70                            = 0xB596;
const RGB16Color GRAY_75                            = 0xBDF7;
const RGB16Color GRAY_80                            = 0xCE79;
const RGB16Color GRAY_85                            = 0xDEDB;
const RGB16Color GRAY_90                            = 0xE73C;
const RGB16Color GRAY_95                            = 0xF79E;

/*----------------------------------------------------------------------------*/

const RGB16Color kLayerColorPalette[] = {
    WHITE,
    GRAY_75,
    WHITE,
    GRAY_75,
    WHITE,
    GRAY_75,
    WHITE,
    GRAY_75,
    WHITE,
    GRAY_75};

const RGB16Color kBeatSelectedColor                 = MAGENTA;
const RGB16Color kLayerSelectedColor                = CYAN;

const RGB16Color kMenuHLineColor                    = GRAY_50;
const RGB16Color kMenuVLineColor                    = GRAY_50;

/*----------------------------------------------------------------------------*/

typedef enum {
    PORTRAIT_0                                      = 0x00,
    PORTRAIT_1                                      = 0x02,
    LANDSCAPE_0                                     = 0x01,
    LANDSCAPE_1                                     = 0x03
} LcdRotation;

/*----------------------------------------------------------------------------*/

typedef enum {
    FONT_05x07                                      = 0x00,
    FONT_07x09                                      = 0x01,
    FONT_10x14                                      = 0x02,
    FONT_14x18                                      = 0x03
} LcdFont;

/*----------------------------------------------------------------------------*/

typedef enum {
    LEFT                                            = 0x00,
    RIGHT                                           = 0x01,
    CENTER                                          = 0x02
} LcdAlign;

////////////////////////////////////////////////////////////////////////////////
/* Dac Constants -------------------------------------------------------------*/
////////////////////////////////////////////////////////////////////////////////

const uint8_t kHpBase                               = 0x018;

struct MixerHpVolume {
    char nameShortR[5];
    char data;
};

const MixerHpVolume kMixerHpVolumeDataLibrary[] = {
    {" -20", kHpBase + 40}, 
    {" -19", kHpBase + 38}, 
    {" -18", kHpBase + 36}, 
    {" -17", kHpBase + 34}, 
    {" -16", kHpBase + 32}, 
    {" -15", kHpBase + 30}, 
    {" -14", kHpBase + 28}, 
    {" -13", kHpBase + 26}, 
    {" -12", kHpBase + 24}, 
    {" -11", kHpBase + 22}, 
    {" -10", kHpBase + 20}, 
    {" -09", kHpBase + 18}, 
    {" -08", kHpBase + 16}, 
    {" -07", kHpBase + 14}, 
    {" -06", kHpBase + 12}, 
    {" -05", kHpBase + 10}, 
    {" -04", kHpBase + 8}, 
    {" -03", kHpBase + 6}, 
    {" -02", kHpBase + 4}, 
    {" -01", kHpBase + 2}, 
    {" +00", kHpBase}};

const uint8_t kLoBase                               = 0x0D;

struct MixerLoVolume {
    char nameShortR[5];
    char data;
};

const MixerLoVolume kMixerLoVolumeDataLibrary[] = {
    {" -20", kLoBase + 18}, 
    {" -19", kLoBase + 18}, 
    {" -18", kLoBase + 18}, 
    {" -17", kLoBase + 17}, 
    {" -16", kLoBase + 16}, 
    {" -15", kLoBase + 15}, 
    {" -14", kLoBase + 14}, 
    {" -13", kLoBase + 13}, 
    {" -12", kLoBase + 12}, 
    {" -11", kLoBase + 11}, 
    {" -10", kLoBase + 10}, 
    {" -09", kLoBase + 9}, 
    {" -08", kLoBase + 8}, 
    {" -07", kLoBase + 7}, 
    {" -06", kLoBase + 6}, 
    {" -05", kLoBase + 5}, 
    {" -04", kLoBase + 4}, 
    {" -03", kLoBase + 3}, 
    {" -02", kLoBase + 2}, 
    {" -01", kLoBase + 1}, 
    {" +00", kLoBase}};

const float kLiBase                                 = 1.0f;

struct MixerLiVolume {
    char nameShortR[5];
    float data;
};

const MixerLiVolume kMixerLiVolumeDataLibrary[] = {
    {" -20", kLiBase * 0.1000000f}, 
    {" -19", kLiBase * 0.1122018f}, 
    {" -18", kLiBase * 0.1258925f}, 
    {" -17", kLiBase * 0.1412538f}, 
    {" -16", kLiBase * 0.1584893f}, 
    {" -15", kLiBase * 0.1778279f}, 
    {" -14", kLiBase * 0.1995262f}, 
    {" -13", kLiBase * 0.2238721f}, 
    {" -12", kLiBase * 0.2511886f}, 
    {" -11", kLiBase * 0.2818383f}, 
    {" -10", kLiBase * 0.3162278f}, 
    {" -09", kLiBase * 0.3548134f}, 
    {" -08", kLiBase * 0.3981072f}, 
    {" -07", kLiBase * 0.4466836f}, 
    {" -06", kLiBase * 0.5011872f}, 
    {" -05", kLiBase * 0.5623413f}, 
    {" -04", kLiBase * 0.6309573f}, 
    {" -03", kLiBase * 0.7079458f}, 
    {" -02", kLiBase * 0.7943282f}, 
    {" -01", kLiBase * 0.8912509f}, 
    {" +00", kLiBase * 1.0000000f}};

// clang-format on

#endif
