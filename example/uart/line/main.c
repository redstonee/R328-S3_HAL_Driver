#include "example.h"

#define LINE_CAPACITY 64U
#define LINE_IDLE_TIMEOUT_MS 5000U

int main(void)
{
    Example_Init("UART0 / bounded line input");
    (void)Board_ConsoleWrite(
        "Enter a line; CR/LF completes, Backspace edits, idle timeout is 5 s.\r\n");
    uint8_t line[LINE_CAPACITY];
    uint32_t used = 0U, overflow = 0U, skip_lf = 0U, last_byte = HAL_GetTick();
    for (;;)
    {
        uint8_t byte;
        HAL_StatusTypeDef status = HAL_UART_Receive(&huart0, &byte, 1U, 0U);
        if (status == HAL_OK)
        {
            last_byte = HAL_GetTick();
            if (byte == '\n' && skip_lf != 0U)
            {
                skip_lf = 0U;
                continue;
            }
            skip_lf = byte == '\r';
            if (byte == '\r' || byte == '\n')
            {
                (void)Board_ConsoleWrite("\r\n");
                if (overflow != 0U)
                {
                    (void)Board_ConsoleWrite("Line discarded: capacity is 63 bytes.\r\n");
                }
                else
                {
                    Example_PrintValue("line bytes=", used);
                    Example_RequireOK("UART TX=", HAL_UART_Transmit(&huart0, line, used, 100U));
                    (void)Board_ConsoleWrite("\r\n");
                }
                used = 0U;
                overflow = 0U;
            }
            else if ((byte == 8U || byte == 127U) && overflow == 0U && used != 0U)
            {
                --used;
                (void)Board_ConsoleWrite("\b \b");
            }
            else if (byte >= 32U && byte <= 126U && overflow == 0U)
            {
                if (used < sizeof(line) - 1U)
                {
                    line[used++] = byte;
                    Example_RequireOK("UART TX=", HAL_UART_Transmit(&huart0, &byte, 1U, 100U));
                }
                else
                {
                    overflow = 1U; /* Consume through the terminator; never execute a prefix. */
                }
            }
        }
        else if (status != HAL_TIMEOUT && status != HAL_BUSY)
        {
            Example_Report("UART RX=", status);
            Example_PrintValue("UART ErrorCode=", huart0.ErrorCode);
            used = 0U;
            overflow = 0U;
        }
        if ((used != 0U || overflow != 0U) &&
            (uint32_t)(HAL_GetTick() - last_byte) >= LINE_IDLE_TIMEOUT_MS)
        {
            used = 0U;
            overflow = 0U;
            (void)Board_ConsoleWrite("\r\nPartial line discarded: idle timeout.\r\n");
        }
    }
}
