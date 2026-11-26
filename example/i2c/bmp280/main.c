#include "main.h"
#include "bmp280.h"

#define UART_TIMEOUT_MS 100U
static UART_HandleTypeDef huart0;
volatile uint32_t g_application_error;

void Error_Handler(void)
{
    g_application_error = 1U;
    R328_IRQ_Disable();
    for (;;)
    {
        R328_WFI();
    }
}
void SystemClock_Config(void)
{
    if (HAL_RCC_APB2_Config24M() != HAL_OK)
    {
        Error_Handler();
    }
    SystemCoreClockUpdate();
}
static void require_ok(const char *label, HAL_StatusTypeDef status)
{
    if (status != HAL_OK)
    {
        (void)HAL_UART_PrintValue(&huart0, label, (uint32_t)status);
        Error_Handler();
    }
}
static void app_init(const char *banner)
{
    if (HAL_Init() != HAL_OK)
    {
        Error_Handler();
    }
    SystemClock_Config();
    HAL_UART_SetPollingTimeout(UART_TIMEOUT_MS);
    /* This example explicitly chooses UART0 PH0/PH1, 115200 8N1 for diagnostics.
     * The framework itself does not reserve any UART or initialize these pins. */
    GPIO_InitTypeDef uart_pins = {.Pin = GPIO_PIN_0 | GPIO_PIN_1,
                                  .Mode = GPIO_MODE_AF_PP,
                                  .Pull = GPIO_PULLUP,
                                  .Drive = GPIO_DRIVE_LEVEL1,
                                  .Alternate = 3U};
    if (HAL_GPIO_Init(GPIOH, &uart_pins) != HAL_OK)
    {
        Error_Handler();
    }
    huart0.Instance = UART0;
    huart0.Init =
        (UART_InitTypeDef){115200U, UART_WORDLENGTH_8B, UART_STOPBITS_1, UART_PARITY_NONE};
    require_ok("UART init=", HAL_UART_Init(&huart0));
    (void)HAL_UART_PrintString(&huart0, "\r\nR328 example: ");
    (void)HAL_UART_PrintString(&huart0, banner);
    (void)HAL_UART_PrintString(&huart0, "\r\nstatus: 0=OK 1=ERROR 2=BUSY 3=TIMEOUT\r\n");
}

static I2C_HandleTypeDef hi2c1;
static HAL_StatusTypeDef i2c_init(uint32_t clock_speed)
{
    if (hi2c1.State == HAL_I2C_STATE_BUSY)
    {
        return HAL_BUSY;
    }
    /* PB8 SCL / PB9 SDA, AF4. The TWI function controls open-drain output. */
    GPIO_InitTypeDef pins = {.Pin = GPIO_PIN_8 | GPIO_PIN_9,
                             .Mode = GPIO_MODE_AF_PP,
                             .Pull = GPIO_PULLUP,
                             .Drive = GPIO_DRIVE_LEVEL1,
                             .Alternate = 4U};
    HAL_StatusTypeDef status = HAL_GPIO_Init(GPIOB, &pins);
    if (status != HAL_OK)
    {
        return status;
    }
    hi2c1.Instance = I2C1;
    hi2c1.Init =
        (I2C_InitTypeDef){.ClockSpeed = clock_speed, .AddressingMode = I2C_ADDRESSINGMODE_7BIT};
    return HAL_I2C_Init(&hi2c1);
}

#define BMP280_I2C_CLOCK_HZ 100000U
#define BMP280_I2C_TIMEOUT_MS 100U
#define BMP280_RESET_DELAY_MS 5U
#define BMP280_NVM_TIMEOUT_MS 100U
#define BMP280_CONVERSION_DELAY_MS 10U /* Temperature x1 / pressure x1. */
#define BMP280_TX_DMA_CHANNEL 0U
#define BMP280_RX_DMA_CHANNEL 1U

static DMA_HandleTypeDef tx_dma = {
    .Init = {BMP280_TX_DMA_CHANNEL, DMA_MEMORY_TO_PERIPH, DMA_REQUEST_TWI1}};
static DMA_HandleTypeDef rx_dma = {
    .Init = {BMP280_RX_DMA_CHANNEL, DMA_PERIPH_TO_MEMORY, DMA_REQUEST_TWI1}};
static BMP280_Calibration calibration;
/* All DMA buffers are writable SRAM/DDR objects, never flash or temporaries. */
static uint8_t registers[BMP280_CALIBRATION_SIZE], command, status_byte, chip_id;
static uint8_t mode = 'p';
static volatile uint32_t completed, failed;

void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *h)
{
    if (h == &hi2c1)
    {
        completed = 1U;
    }
}
void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *h)
{
    if (h == &hi2c1)
    {
        completed = 1U;
    }
}
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *h)
{
    if (h == &hi2c1)
    {
        failed = 1U;
    }
}
static HAL_StatusTypeDef transfer(uint8_t reg, uint8_t *data, uint32_t size, int read)
{
    const uint16_t address = BMP280_ADDRESS << 1;
    completed = 0U;
    failed = 0U;
    if (mode == 'p')
    {
        return read ? HAL_I2C_Mem_Read(&hi2c1, address, reg, I2C_MEMADD_SIZE_8BIT, data, size)
                    : HAL_I2C_Mem_Write(&hi2c1, address, reg, I2C_MEMADD_SIZE_8BIT, data, size);
    }
    HAL_StatusTypeDef result;
    if (mode == 'i')
    {
        result = read
                     ? HAL_I2C_Mem_Read_IT(&hi2c1, address, reg, I2C_MEMADD_SIZE_8BIT, data, size)
                     : HAL_I2C_Mem_Write_IT(&hi2c1, address, reg, I2C_MEMADD_SIZE_8BIT, data, size);
    }
    else
    {
        result =
            read ? HAL_I2C_Mem_Read_DMA(&hi2c1, address, reg, I2C_MEMADD_SIZE_8BIT, data, size)
                 : HAL_I2C_Mem_Write_DMA(&hi2c1, address, reg, I2C_MEMADD_SIZE_8BIT, data, size);
    }
    if (result != HAL_OK)
    {
        return result;
    }
    /* Real applications can run other foreground work here. Process does not
     * move data: IT bytes and DMA completion are serviced by hardware IRQs. */
    while (completed == 0U && failed == 0U)
    {
        HAL_I2C_Process(&hi2c1);
    }
    return failed != 0U
               ? ((hi2c1.ErrorCode & HAL_I2C_ERROR_TIMEOUT) != 0U ? HAL_TIMEOUT : HAL_ERROR)
               : HAL_OK;
}
static int check(HAL_StatusTypeDef result)
{
    if (result == HAL_OK)
    {
        return 1;
    }
    HAL_UART_PrintValue(&huart0, "BMP280 transfer=", result);
    HAL_UART_PrintValue(&huart0, "I2C ErrorCode=", hi2c1.ErrorCode);
    if (hi2c1.State == HAL_I2C_STATE_RESET)
    {
        require_ok("I2C reinit=", i2c_init(BMP280_I2C_CLOCK_HZ));
    }
    return 0;
}
static int sensor_init(void)
{
    if (!check(transfer(BMP280_REG_CHIP_ID, &chip_id, sizeof(chip_id), 1)))
    {
        return 0;
    }
    HAL_UART_PrintValue(&huart0, "chip ID=", chip_id);
    if (chip_id != BMP280_CHIP_ID)
    {
        (void)HAL_UART_PrintString(&huart0, "Expected BMP280 ID 0x58 at address 0x76.\r\n");
        return 0;
    }
    command = BMP280_RESET_COMMAND;
    if (!check(transfer(BMP280_REG_RESET, &command, sizeof(command), 0)))
    {
        return 0;
    }
    HAL_Delay(BMP280_RESET_DELAY_MS);
    uint32_t start = HAL_GetTick();
    do
    {
        if (!check(transfer(BMP280_REG_STATUS, &status_byte, sizeof(status_byte), 1)))
        {
            return 0;
        }
        if ((status_byte & BMP280_STATUS_IM_UPDATE) == 0U)
        {
            break;
        }
        if ((uint32_t)(HAL_GetTick() - start) >= BMP280_NVM_TIMEOUT_MS)
        {
            return 0;
        }
    } while (1);
    if (!check(transfer(BMP280_REG_CALIBRATION, registers, BMP280_CALIBRATION_SIZE, 1)))
    {
        return 0;
    }
    if (!BMP280_DecodeCalibration(&calibration, registers))
    {
        (void)HAL_UART_PrintString(&huart0, "Invalid calibration.\r\n");
        return 0;
    }
    command = BMP280_CONFIG_FILTER_OFF | BMP280_CONFIG_SPI3W_DISABLE;
    return check(transfer(BMP280_REG_CONFIG, &command, sizeof(command), 0));
}
static void measure(void)
{
    if (!sensor_init())
    {
        return;
    }
    command = BMP280_CTRL_OSRS_T_X1 | BMP280_CTRL_OSRS_P_X1 | BMP280_CTRL_MODE_FORCED;
    if (!check(transfer(BMP280_REG_CTRL_MEAS, &command, sizeof(command), 0)))
    {
        return;
    }
    HAL_Delay(BMP280_CONVERSION_DELAY_MS);
    if (!check(transfer(BMP280_REG_STATUS, &status_byte, sizeof(status_byte), 1)))
    {
        return;
    }
    if ((status_byte & BMP280_STATUS_MEASURING) != 0U)
    {
        (void)HAL_UART_PrintString(&huart0, "BMP280 still measuring; retry.\r\n");
        return;
    }
    uint32_t start = HAL_GetTick();
    if (!check(transfer(BMP280_REG_PRESS_MSB, registers, BMP280_SAMPLE_SIZE, 1)))
    {
        return;
    }
    HAL_UART_PrintValue(&huart0, "sample read ms=", HAL_GetTick() - start);
    int32_t temperature;
    uint32_t pressure;
    if (!BMP280_Compensate(&calibration, registers, &temperature, &pressure))
    {
        (void)HAL_UART_PrintString(&huart0, "Invalid sample/calibration.\r\n");
        HAL_UART_HexDump(&huart0, registers, BMP280_SAMPLE_SIZE);
        return;
    }
    (void)HAL_UART_PrintString(&huart0, "Temperature C=");
    uint32_t magnitude = (uint32_t)(temperature < 0 ? -temperature : temperature);
    if (temperature < 0)
    {
        (void)HAL_UART_PrintString(&huart0, "-");
    }
    (void)HAL_UART_PrintU32(&huart0, magnitude / BMP280_TEMPERATURE_SCALE);
    (void)HAL_UART_PrintString(&huart0, ".");
    if (magnitude % BMP280_TEMPERATURE_SCALE < 10U)
    {
        (void)HAL_UART_PrintString(&huart0, "0");
    }
    (void)HAL_UART_PrintU32(&huart0, magnitude % BMP280_TEMPERATURE_SCALE);
    (void)HAL_UART_PrintString(&huart0, "\r\n");
    HAL_UART_PrintValue(&huart0, "Pressure Pa=", pressure);
    HAL_UART_PrintValue(&huart0, "callback complete=", completed);
}
int main(void)
{
    app_init("I2C1 / BMP280 PB8 SCL, PB9 SDA, address 0x76");
    HAL_I2C_SetPollingTimeout(BMP280_I2C_TIMEOUT_MS);
    require_ok("I2C init=", i2c_init(BMP280_I2C_CLOCK_HZ));
    require_ok("TX DMA init=", HAL_DMA_Init(&tx_dma));
    require_ok("RX DMA init=", HAL_DMA_Init(&rx_dma));
    hi2c1.hdmatx = &tx_dma;
    hi2c1.hdmarx = &rx_dma;
    (void)HAL_UART_PrintString(&huart0,
                               "p=polling, i=interrupt, d=DMA; each command measures once.\r\n");
    for (;;)
    {
        HAL_I2C_Process(&hi2c1);
        uint8_t byte;
        if (HAL_UART_GetRxFifoLevel(&huart0) == 0U ||
            HAL_UART_Receive(&huart0, &byte, 1U) != HAL_OK)
        {
            continue;
        }
        if (byte == 'p' || byte == 'i' || byte == 'd')
        {
            mode = byte;
            (void)HAL_UART_PrintString(&huart0, byte == 'p'   ? "Mode=polling\r\n"
                                                : byte == 'i' ? "Mode=interrupt\r\n"
                                                              : "Mode=DMA\r\n");
            measure();
        }
    }
}
