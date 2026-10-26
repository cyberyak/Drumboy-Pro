#include "main.h"

#include "Branding.h"
#include "Controller.h"
#include "PluginHooks.h"
#include "Profiler.h"
#include "bdma.h"
#include "dma.h"
#include "fatfs.h"
#include "fmc.h"
#include "gpio.h"
#include "i2c.h"
#include "i2s.h"
#include "sdmmc.h"
#include "tim.h"
#include "usart.h"

/* Extern Function Declarations ---------------------------------- */

extern "C" void SystemClock_Config();
extern "C" void PeriphCommonClock_Config();
extern "C" void Sdram_Initialize();
extern "C" void System_Initialize();

/* Memory Allocation In D1 Ram ----------------------------------- */

uint32_t i2s_rxBuffer[I2S_TOTAL_BUFFER_SIZE] = {0x00};
uint32_t i2s_txBuffer[I2S_TOTAL_BUFFER_SIZE] = {0x00};

/* Memory Allocation In D3 Ram ----------------------------------- */

__attribute__((section(".RAM_D3"))) uint8_t i2c3_rxData[2] = {0x00};
__attribute__((section(".RAM_D3"))) uint8_t i2c3_txData[2] = {0x00};
__attribute__((section(".RAM_D3"))) uint8_t i2c4_rxData[2] = {0x00};
__attribute__((section(".RAM_D3"))) uint8_t i2c4_txData[2] = {0x00};

/* Sync And Midi Buffers ----------------------------------------- */

uint8_t midiRxData;

/* Global Controller Instance ------------------------------------ */

Controller controller;

int main() {
    // System Startup
    __disable_irq();
    SCB->VTOR = APPLICATION_ADDRESS;
    SCB_InvalidateICache();
    SCB_InvalidateDCache();
    SCB_EnableICache();
    HAL_Init();

    // Enable FPU (Floating Point Unit)
    SCB->CPACR |= ((3UL << 10 * 2) | (3UL << 11 * 2));

    // Enable Hardware FTZ (Flush-to-Zero)
    __set_FPSCR(__get_FPSCR() | (1 << 24) | (1 << 25));
    __enable_irq();

    // Configure Clocks
    SystemClock_Config();
    PeriphCommonClock_Config();

    // Initialize Hardware
    System_Initialize();
    Sdram_Initialize();

    // Initialize Application
    controller.initialize();
    if (controller.getMenu() != TEST_MENU) PLUGIN_INIT(controller);

    // Main Loop
    while (true) {
        PROFILE_LOOP_BEGIN();
        controller.update();
        PROFILE_LOOP_END();
        if ((controller.getMenu() != INIT_MENU) && (controller.getMenu() != TEST_MENU)) {
            PLUGIN_UPDATE(controller);
            BRANDING_DRAW(controller.lcd, controller.getMenu() == FILE_MENU);
        }
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
    MX_USART1_UART_Init();
    MX_USART6_UART_Init();
    MX_TIM4_Init();
    MX_TIM5_Init();
    MX_TIM6_Init();
    MX_TIM7_Init();
    MX_TIM8_Init();
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
    FMC_SDRAM_CommandTypeDef Command;
    __IO uint32_t tmpmrd = 0;

    // 1. Clock Enable
    Command.CommandMode = FMC_SDRAM_CMD_CLK_ENABLE;
    Command.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
    Command.AutoRefreshNumber = 1;
    Command.ModeRegisterDefinition = 0;
    HAL_SDRAM_SendCommand(hsdram, &Command, SDRAM_TIMEOUT);
    HAL_Delay(1);

    // 2. Precharge All
    Command.CommandMode = FMC_SDRAM_CMD_PALL;
    Command.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
    Command.AutoRefreshNumber = 1;
    Command.ModeRegisterDefinition = 0;
    HAL_SDRAM_SendCommand(hsdram, &Command, SDRAM_TIMEOUT);

    // 3. Auto Refresh
    Command.CommandMode = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
    Command.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
    Command.AutoRefreshNumber = 4;
    Command.ModeRegisterDefinition = 0;
    HAL_SDRAM_SendCommand(hsdram, &Command, SDRAM_TIMEOUT);

    // 4. Load Mode Register
    tmpmrd = (uint32_t)SDRAM_MODEREG_BURST_LENGTH_2 | SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL | SDRAM_MODEREG_CAS_LATENCY_2 | SDRAM_MODEREG_OPERATING_MODE_STANDARD | SDRAM_MODEREG_WRITEBURST_MODE_SINGLE;
    Command.CommandMode = FMC_SDRAM_CMD_LOAD_MODE;
    Command.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
    Command.AutoRefreshNumber = 1;
    Command.ModeRegisterDefinition = tmpmrd;
    HAL_SDRAM_SendCommand(hsdram, &Command, SDRAM_TIMEOUT);

    // 5. Set Refresh Rate
    HAL_SDRAM_ProgramRefreshRate(hsdram, 1854);
}

/* Gpio Interrupts ----------------------------------------------- */

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (controller.getMenu() != INIT_MENU) {
        switch (GPIO_Pin) {
            case MX_X_INT_Pin:
                controller.interruptLayerKeypadRead();
                break;
            case MX_A_INT_Pin:
                controller.interruptLeftKeypadRead();
                break;
            case MX_B_INT_Pin:
                controller.interruptRightKeypadRead();
                break;
            case ENCO_0A_Pin:
                controller.interruptEncoderRead(0);
                break;
            case ENCO_1A_Pin:
                controller.interruptEncoderRead(1);
                break;
            case ENCO_2A_Pin:
                controller.interruptEncoderRead(2);
                break;
            case ENCO_3A_Pin:
                controller.interruptEncoderRead(3);
                break;
            case ENCO_4A_Pin:
                controller.interruptEncoderRead(4);
                break;
            case ENCO_5A_Pin:
                controller.interruptEncoderRead(5);
                break;
            case ENCO_6A_Pin:
                controller.interruptEncoderRead(6);
                break;
            case ENCO_7A_Pin:
                controller.interruptEncoderRead(7);
                break;
            case BUTTON_Pin:
                controller.interruptFuncButtonRead();
                break;
            case SYNC_IN_PULSE_Pin:
                controller.interruptSyncInPulse();
                break;
        }
    }
}

/* Timer Interrupts ---------------------------------------------- */

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef* htim) {
    if (htim->Instance == TIM4) controller.interruptMidiTxClock();
    if (htim->Instance == TIM5) controller.interruptTransition();
    if (htim->Instance == TIM6) controller.interruptSystemPreset();
    if (htim->Instance == TIM7) controller.interruptEncoderPreset();
    if (htim->Instance == TIM8) controller.interruptLedEq();
    if (htim->Instance == TIM12) controller.interruptUpKeyRead();
    if (htim->Instance == TIM13) controller.interruptDownKeyRead();
    if (htim->Instance == TIM14) controller.interruptLongKeyRead();
    if (htim->Instance == TIM15) controller.interruptPlay();
    if (htim->Instance == TIM16) controller.interruptText();
    if (htim->Instance == TIM17) controller.interruptSd();
    if (htim->Instance == TIM23) controller.interruptBeatSync();
    if (htim->Instance == TIM24) controller.interruptLimitAlert();
}

void HAL_TIM_ErrorCallback(TIM_HandleTypeDef* htim) {}

/* I2s Interrupts ------------------------------------------------ */

// 1. Half Transfer (Process First 32 Samples)
void HAL_I2SEx_TxRxHalfCpltCallback(I2S_HandleTypeDef* hi2s) {
    if (hi2s->Instance == SPI3) {
        PROFILE_AUDIO_BEGIN();
        controller.processAudioBlock(&i2s_rxBuffer[0], &i2s_txBuffer[0]);
        PROFILE_AUDIO_END();
    }
}

// 2. Complete Transfer (Process Second 32 Samples)
void HAL_I2SEx_TxRxCpltCallback(I2S_HandleTypeDef* hi2s) {
    if (hi2s->Instance == SPI3) {
        PROFILE_AUDIO_BEGIN();
        controller.processAudioBlock(&i2s_rxBuffer[I2S_BLOCK_SIZE * 2], &i2s_txBuffer[I2S_BLOCK_SIZE * 2]);
        PROFILE_AUDIO_END();
    }
}

void HAL_I2SEx_TxRxErrorCallback(I2S_HandleTypeDef* hi2s) {
    if (hi2s->Instance == SPI3) {
    }
}

/* I2c Interrupts ------------------------------------------------ */

void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef* hi2c) {
    if (hi2c->Instance == I2C3) {
    }
    if (hi2c->Instance == I2C4) {
    }
}

void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef* hi2c) {
    if (hi2c->Instance == I2C3) {
    }
    if (hi2c->Instance == I2C4) {
    }
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef* hi2c) {}

/* Uart Interrupts ----------------------------------------------- */

void HAL_UART_TxCpltCallback(UART_HandleTypeDef* huart) {
    if (huart == &huart6) {
        controller.serviceMidiTx();
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef* huart) {
    if (huart == &huart1) {
        controller.receiveMidiCommand();
        HAL_UART_Receive_DMA(&huart1, &midiRxData, 1);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef* huart) {
    if (huart == &huart1) {
        HAL_UART_Receive_DMA(&huart1, &midiRxData, 1);
    }
}

void Error_Handler(void) {
    __disable_irq();
    while (1) {
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t* file, uint32_t line) {}
#endif
