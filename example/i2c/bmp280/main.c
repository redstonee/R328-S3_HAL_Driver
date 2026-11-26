#include "example.h"
#include "bmp280.h"

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
        return read ? HAL_I2C_Mem_Read(&hi2c1, address, reg, I2C_MEMADD_SIZE_8BIT, data, size,
                                       BMP280_I2C_TIMEOUT_MS)
                    : HAL_I2C_Mem_Write(&hi2c1, address, reg, I2C_MEMADD_SIZE_8BIT, data, size,
                                        BMP280_I2C_TIMEOUT_MS);
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
    Example_Report("BMP280 transfer=", result);
    Example_PrintValue("I2C ErrorCode=", hi2c1.ErrorCode);
    if (hi2c1.State == HAL_I2C_STATE_RESET)
    {
        Example_RequireOK("I2C reinit=", Board_I2C1_Init(BMP280_I2C_CLOCK_HZ));
    }
    return 0;
}
static int sensor_init(void)
{
    if (!check(transfer(BMP280_REG_CHIP_ID, &chip_id, sizeof(chip_id), 1)))
    {
        return 0;
    }
    Example_PrintValue("chip ID=", chip_id);
    if (chip_id != BMP280_CHIP_ID)
    {
        (void)Board_ConsoleWrite("Expected BMP280 ID 0x58 at address 0x76.\r\n");
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
        (void)Board_ConsoleWrite("Invalid calibration.\r\n");
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
        (void)Board_ConsoleWrite("BMP280 still measuring; retry.\r\n");
        return;
    }
    uint32_t start = HAL_GetTick();
    if (!check(transfer(BMP280_REG_PRESS_MSB, registers, BMP280_SAMPLE_SIZE, 1)))
    {
        return;
    }
    Example_PrintValue("sample read ms=", HAL_GetTick() - start);
    int32_t temperature;
    uint32_t pressure;
    if (!BMP280_Compensate(&calibration, registers, &temperature, &pressure))
    {
        (void)Board_ConsoleWrite("Invalid sample/calibration.\r\n");
        Example_Dump(registers, BMP280_SAMPLE_SIZE);
        return;
    }
    (void)Board_ConsoleWrite("Temperature C=");
    uint32_t magnitude = (uint32_t)(temperature < 0 ? -temperature : temperature);
    if (temperature < 0)
    {
        (void)Board_ConsoleWrite("-");
    }
    (void)Board_ConsoleWriteU32(magnitude / BMP280_TEMPERATURE_SCALE);
    (void)Board_ConsoleWrite(".");
    if (magnitude % BMP280_TEMPERATURE_SCALE < 10U)
    {
        (void)Board_ConsoleWrite("0");
    }
    (void)Board_ConsoleWriteU32(magnitude % BMP280_TEMPERATURE_SCALE);
    (void)Board_ConsoleWrite("\r\n");
    Example_PrintValue("Pressure Pa=", pressure);
    Example_PrintValue("callback complete=", completed);
}
int main(void)
{
    Example_Init("I2C1 / BMP280 PB8 SCL, PB9 SDA, address 0x76");
    Example_RequireOK("I2C init=", Board_I2C1_Init(BMP280_I2C_CLOCK_HZ));
    Example_RequireOK("TX DMA init=", HAL_DMA_Init(&tx_dma));
    Example_RequireOK("RX DMA init=", HAL_DMA_Init(&rx_dma));
    hi2c1.hdmatx = &tx_dma;
    hi2c1.hdmarx = &rx_dma;
    (void)Board_ConsoleWrite("p=polling, i=interrupt, d=DMA; each command measures once.\r\n");
    for (;;)
    {
        HAL_I2C_Process(&hi2c1);
        uint8_t byte;
        if (HAL_UART_Receive(&huart0, &byte, 1U, 0U) != HAL_OK)
        {
            continue;
        }
        if (byte == 'p' || byte == 'i' || byte == 'd')
        {
            mode = byte;
            (void)Board_ConsoleWrite(byte == 'p'   ? "Mode=polling\r\n"
                                     : byte == 'i' ? "Mode=interrupt\r\n"
                                                   : "Mode=DMA\r\n");
            measure();
        }
    }
}
