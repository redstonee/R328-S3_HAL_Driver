#ifndef R328_H
#define R328_H

#include <stddef.h>
#include <stdint.h>
#include "r328_core.h"

#define __IO volatile
#define R328_OSC24M_HZ 24000000U
/* R328 system timestamp clock: 24 MHz.
 * Independent of the CPU PLL/APB clocks; CNTFRQ is only firmware metadata. */
#define R328_ARCH_COUNTER_HZ 24000000U
#define R328_CCU_BASE 0x03001000UL
#define R328_GPIO_BASE 0x0300B000UL
#define R328_TIMER_BASE 0x03009000UL
#define R328_UART0_BASE 0x05000000UL
#define R328_GICD_BASE 0x03021000UL
#define R328_GICC_BASE 0x03022000UL
#define R328_REG32(address) (*(__IO uint32_t *)(uintptr_t)(address))
#define R328_CCU_REG(offset) R328_REG32(R328_CCU_BASE + (offset))
#define RCC_CPUX_CFG R328_CCU_REG(0x0500U)
#define RCC_APB2_CFG R328_CCU_REG(0x0524U)
#define RCC_UART_BGR R328_CCU_REG(0x090CU)
#define RCC_TWI_BGR R328_CCU_REG(0x091CU)
#define RCC_SPI_CLK(index) R328_CCU_REG(0x0940U + 4U * (index))
#define RCC_SPI_BGR R328_CCU_REG(0x096CU)
#define RCC_I2S_CLK(index) R328_CCU_REG(0x0A10U + 4U * (index))
#define RCC_I2S_BGR R328_CCU_REG(0x0A1CU)
#define RCC_PLL_AUDIO_CTRL R328_CCU_REG(0x0078U)
#define RCC_PLL_AUDIO_PAT0 R328_CCU_REG(0x0178U)

typedef struct
{
    __IO uint32_t CFG[4];
    __IO uint32_t DAT;
    __IO uint32_t DRV[2];
    __IO uint32_t PUL[2];
} GPIO_TypeDef;
#define GPIOB ((GPIO_TypeDef *)(R328_GPIO_BASE + 1U * 0x24U))
#define GPIOC ((GPIO_TypeDef *)(R328_GPIO_BASE + 2U * 0x24U))
#define GPIOE ((GPIO_TypeDef *)(R328_GPIO_BASE + 4U * 0x24U))
#define GPIOG ((GPIO_TypeDef *)(R328_GPIO_BASE + 6U * 0x24U))
#define GPIOH ((GPIO_TypeDef *)(R328_GPIO_BASE + 7U * 0x24U))

/* GPIO interrupt banks: manual 6.6.5 gives explicit PB/PE/PG/PH offsets. */
typedef struct
{
    __IO uint32_t CFG[4];
    __IO uint32_t CTL;
    __IO uint32_t STATUS;
    __IO uint32_t DEB;
    uint32_t RESERVED;
} GPIO_EXTI_TypeDef;
#define GPIOB_EXTI ((GPIO_EXTI_TypeDef *)(R328_GPIO_BASE + 0x220U))
#define GPIOE_EXTI ((GPIO_EXTI_TypeDef *)(R328_GPIO_BASE + 0x280U))
#define GPIOG_EXTI ((GPIO_EXTI_TypeDef *)(R328_GPIO_BASE + 0x2C0U))
#define GPIOH_EXTI ((GPIO_EXTI_TypeDef *)(R328_GPIO_BASE + 0x2E0U))

typedef struct
{
    __IO uint32_t DATA; /* RBR / THR / DLL, selected by LCR.DLAB */
    __IO uint32_t IER_DLH;
    __IO uint32_t IIR_FCR;
    __IO uint32_t LCR;
    __IO uint32_t MCR;
    __IO uint32_t LSR;
    __IO uint32_t MSR;
    __IO uint32_t SCR;
    uint32_t RESERVED0[23];
    __IO uint32_t USR;
    __IO uint32_t TFL;
    __IO uint32_t RFL;
    __IO uint32_t HSK;
    uint32_t RESERVED1[6];
    __IO uint32_t HALT;
} UART_TypeDef;
#define UART0 ((UART_TypeDef *)(R328_UART0_BASE + 0x000U))
#define UART1 ((UART_TypeDef *)(R328_UART0_BASE + 0x400U))
#define UART2 ((UART_TypeDef *)(R328_UART0_BASE + 0x800U))
#define UART3 ((UART_TypeDef *)(R328_UART0_BASE + 0xC00U))

typedef struct
{
    uint32_t RESERVED0;
    __IO uint32_t GCR, TCR;
    uint32_t RESERVED1;
    __IO uint32_t IER, ISR, FCR, FSR, WCR, CCR;
    uint32_t RESERVED2[2];
    __IO uint32_t MBC, MTC, BCC;
    uint32_t RESERVED3[(0x200U - 0x3CU) / 4U];
    __IO uint32_t TXD;
    uint32_t RESERVED4[(0x300U - 0x204U) / 4U];
    __IO uint32_t RXD;
} SPI_TypeDef;
#define SPI0 ((SPI_TypeDef *)0x05010000UL)
#define SPI1 ((SPI_TypeDef *)0x05011000UL)

typedef struct
{
    __IO uint32_t ADDR, XADDR, DATA, CNTR, STAT, CCR, SRST, EFR, LCR;
    uint32_t RESERVED[(0x200U - 0x24U) / 4U];
    __IO uint32_t DRV_CTRL;
} I2C_TypeDef;
#define I2C0 ((I2C_TypeDef *)0x05002000UL)
#define I2C1 ((I2C_TypeDef *)0x05002400UL)
#define TWI0 I2C0
#define TWI1 I2C1

typedef struct
{
    __IO uint32_t CTL, FMT0, FMT1, ISTA, RXFIFO, FCTL, FSTA, INTCTL;
    __IO uint32_t TXFIFO, CLKD, TXCNT, RXCNT, CHCFG;
    __IO uint32_t TXCHSEL[4];
    __IO uint32_t TXCHMAP[4][2];
    __IO uint32_t RXCHSEL, RXCHMAP[4];
} I2S_TypeDef;
#define I2S0 ((I2S_TypeDef *)0x05090000UL)
#define I2S1 ((I2S_TypeDef *)0x05091000UL)
#define I2S2 ((I2S_TypeDef *)0x05092000UL)

typedef struct
{
    __IO uint32_t CTRL;
    __IO uint32_t INTV;
    __IO uint32_t CUR;
    uint32_t RESERVED;
} TIM_TypeDef;
typedef struct
{
    __IO uint32_t IRQ_EN;
    __IO uint32_t IRQ_STA;
    uint32_t RESERVED[2];
    TIM_TypeDef CHANNEL[2];
} TIMER_TypeDef;
#define TIMER ((TIMER_TypeDef *)R328_TIMER_BASE)
#define TIM0 (&TIMER->CHANNEL[0])
#define TIM1 (&TIMER->CHANNEL[1])

/* Absolute GIC interrupt IDs, including the 32 SGI/PPI IDs. Manual 3.8.1. */
typedef enum
{
    GPIOB_IRQn = 75,
    GPIOE_IRQn = 76,
    GPIOG_IRQn = 78,
    GPIOH_IRQn = 79,
    I2S0_IRQn = 99,
    I2S1_IRQn = 100,
    I2S2_IRQn = 101,
    TWI0_IRQn = 102,
    TWI1_IRQn = 103,
    UART0_IRQn = 108,
    UART1_IRQn = 109,
    UART2_IRQn = 110,
    UART3_IRQn = 111,
    SPI0_IRQn = 113,
    SPI1_IRQn = 114,
    TIM0_IRQn = 117,
    TIM1_IRQn = 118
} IRQn_Type;
#define R328_IRQ_COUNT 169U

_Static_assert(sizeof(GPIO_TypeDef) == 0x24, "GPIO stride");
_Static_assert(offsetof(GPIO_EXTI_TypeDef, STATUS) == 0x14, "GPIO EINT status offset");
_Static_assert(sizeof(GPIO_EXTI_TypeDef) == 0x20, "GPIO EINT stride");
_Static_assert(offsetof(UART_TypeDef, USR) == 0x7C, "UART USR offset");
_Static_assert(offsetof(UART_TypeDef, HALT) == 0xA4, "UART HALT offset");
_Static_assert(offsetof(TIMER_TypeDef, CHANNEL[1]) == 0x20, "TIM1 offset");
_Static_assert(offsetof(SPI_TypeDef, TXD) == 0x200, "SPI TXD offset");
_Static_assert(offsetof(SPI_TypeDef, RXD) == 0x300, "SPI RXD offset");
_Static_assert(offsetof(I2C_TypeDef, DRV_CTRL) == 0x200, "TWI driver offset");
_Static_assert(offsetof(I2S_TypeDef, RXCHSEL) == 0x64, "I2S RX select offset");
_Static_assert(offsetof(I2S_TypeDef, RXCHMAP[3]) == 0x74, "I2S RX mapping offset");

extern uint32_t SystemCoreClock;
void SystemInit(void);
void SystemCoreClockUpdate(void);
#endif
