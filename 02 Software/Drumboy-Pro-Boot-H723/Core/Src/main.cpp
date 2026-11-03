#include "main.h"

#include "Controller.h"
#include "bdma.h"
#include "dma.h"
#include "fatfs.h"
#include "fmc.h"
#include "gpio.h"
#include "i2c.h"
#include "i2s.h"
#include "sdmmc.h"
#include "tim.h"

/* Extern Function Declarations ---------------------------------- */

extern "C" void SystemClock_Config();
extern "C" void PeriphCommonClock_Config();
extern "C" void System_Initialize();
extern "C" void Sdram_Initialize();

/* Boot Buffers -------------------------------------------------- */

uint8_t dataSd[1024];
uint8_t dataFlash[1024];

/* Global Controller Instance ------------------------------------ */

Controller controller;

int main() {
    // System Startup
    HAL_Init();

    // Configure Clocks
    SystemClock_Config();
    PeriphCommonClock_Config();

    // Initialize Hardware
    System_Initialize();
    Sdram_Initialize();

    // Initialize Boot Controller
    controller.initialize();

    // Main Loop
    while (true) {
    }
}

extern "C" void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /** Supply configuration update enable
     */
    HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

    /** Configure the main internal regulator output voltage
     */
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {
    }

    /** Initializes the RCC Oscillators according to the specified parameters
     * in the RCC_OscInitTypeDef structure.
     */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 1;
    RCC_OscInitStruct.PLL.PLLN = 68;
    RCC_OscInitStruct.PLL.PLLP = 1;
    RCC_OscInitStruct.PLL.PLLQ = 3;
    RCC_OscInitStruct.PLL.PLLR = 2;
    RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
    RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
    RCC_OscInitStruct.PLL.PLLFRACN = 6144;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    /** Initializes the CPU, AHB and APB buses clocks
     */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
    RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK) {
        Error_Handler();
    }

    /** Enables the Clock Security System
     */
    HAL_RCC_EnableCSS();
}

extern "C" void PeriphCommonClock_Config(void) {
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

    /** Initializes the peripherals clock
     */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_FMC | RCC_PERIPHCLK_SDMMC;
    PeriphClkInitStruct.PLL2.PLL2M = 1;
    PeriphClkInitStruct.PLL2.PLL2N = 30;
    PeriphClkInitStruct.PLL2.PLL2P = 1;
    PeriphClkInitStruct.PLL2.PLL2Q = 2;
    PeriphClkInitStruct.PLL2.PLL2R = 1;
    PeriphClkInitStruct.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_3;
    PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
    PeriphClkInitStruct.PLL2.PLL2FRACN = 0;
    PeriphClkInitStruct.FmcClockSelection = RCC_FMCCLKSOURCE_PLL2;
    PeriphClkInitStruct.SdmmcClockSelection = RCC_SDMMCCLKSOURCE_PLL2;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK) {
        Error_Handler();
    }
}

extern "C" void System_Initialize() {
    // Initialize HAL Modules
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_BDMA_Init();
    MX_FMC_Init();
    MX_FATFS_Init();
    MX_I2C3_Init();
    MX_I2C4_Init();
    MX_I2S3_Init();
    // MX_SDMMC2_SD_Init();
    MX_TIM4_Init();
    MX_TIM5_Init();
    MX_TIM12_Init();
    MX_TIM13_Init();
    MX_TIM14_Init();
    MX_TIM15_Init();
    MX_TIM16_Init();
    MX_TIM17_Init();
    MX_TIM23_Init();
    MX_TIM24_Init();
}

extern "C" void Sdram_Initialize() {
    // Configure Sdram
    SDRAM_HandleTypeDef* hsdram = &hsdram1;
    FMC_SDRAM_CommandTypeDef command = {0};
    FMC_SDRAM_CommandTypeDef* Command = &command;

    // 1. Clock Enable
    __IO uint32_t tmpmrd = 0;
    Command->CommandMode = FMC_SDRAM_CMD_CLK_ENABLE;
    Command->CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
    Command->AutoRefreshNumber = 1;
    Command->ModeRegisterDefinition = 0;
    HAL_SDRAM_SendCommand(hsdram, Command, SDRAM_TIMEOUT);
    HAL_Delay(1);

    // 2. Precharge All
    Command->CommandMode = FMC_SDRAM_CMD_PALL;
    Command->CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
    Command->AutoRefreshNumber = 1;
    Command->ModeRegisterDefinition = 0;
    HAL_SDRAM_SendCommand(hsdram, Command, SDRAM_TIMEOUT);

    // 3. Auto Refresh
    Command->CommandMode = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
    Command->CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
    Command->AutoRefreshNumber = 4;
    Command->ModeRegisterDefinition = 0;
    HAL_SDRAM_SendCommand(hsdram, Command, SDRAM_TIMEOUT);

    // 4. Load Mode Register
    tmpmrd = (uint32_t)SDRAM_MODEREG_BURST_LENGTH_2 | SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL | SDRAM_MODEREG_CAS_LATENCY_2 | SDRAM_MODEREG_OPERATING_MODE_STANDARD | SDRAM_MODEREG_WRITEBURST_MODE_SINGLE;
    Command->CommandMode = FMC_SDRAM_CMD_LOAD_MODE;
    Command->CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
    Command->AutoRefreshNumber = 1;
    Command->ModeRegisterDefinition = tmpmrd;
    HAL_SDRAM_SendCommand(hsdram, Command, SDRAM_TIMEOUT);

    // 5. Set Refresh Rate
    HAL_SDRAM_ProgramRefreshRate(hsdram, 1854);
}

void Error_Handler(void) {
    __disable_irq();
    while (1) {
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t* file, uint32_t line) {}
#endif
