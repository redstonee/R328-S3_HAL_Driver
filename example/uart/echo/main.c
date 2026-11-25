#include "example.h"

int main(void)
{
    Example_Init("UART0 polling echo / 115200 8N1");
    (void)Board_ConsoleWrite("Type to echo. UART pins follow R328_UART0_ROUTE.\r\n");
    Example_Idle();
}
