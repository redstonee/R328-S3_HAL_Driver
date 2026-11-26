#ifndef R328_HAL_UART_H
#define R328_HAL_UART_H
#include "r328_hal_dma.h"

#define UART_WORDLENGTH_7B 2U
#define UART_WORDLENGTH_8B 3U
#define UART_STOPBITS_1 0U
#define UART_STOPBITS_2 (1U << 2)
#define UART_PARITY_NONE 0U
#define UART_PARITY_ODD (1U << 3)
#define UART_PARITY_EVEN ((1U << 3) | (1U << 4))
#define HAL_UART_ERROR_NONE 0U
#define HAL_UART_ERROR_ORE (1U << 1)
#define HAL_UART_ERROR_PE (1U << 2)
#define HAL_UART_ERROR_FE (1U << 3)
#define HAL_UART_ERROR_BI (1U << 4)
#define HAL_UART_ERROR_DMA (1U << 5)
#define HAL_UART_ERROR_TIMEOUT (1U << 6)
#define HAL_UART_ERROR_ABORT (1U << 7)
typedef enum
{
    HAL_UART_STATE_RESET = 0,
    HAL_UART_STATE_READY,
    HAL_UART_STATE_BUSY_TX,
    HAL_UART_STATE_BUSY_RX,
    HAL_UART_STATE_BUSY_TX_RX
} HAL_UART_StateTypeDef;
typedef struct
{
    uint32_t BaudRate;
    uint32_t WordLength;
    uint32_t StopBits;
    uint32_t Parity;
} UART_InitTypeDef;
/* Driver-owned progress for each independent direction. Count is the number of
 * bytes not yet copied to/from the FIFO (DMA count is refreshed on IRQ/Process). */
typedef struct
{
    uint32_t Size, Start;
    volatile uint32_t Count, Mode, ErrorCode, DmaDone;
} UART_TransferTypeDef;
typedef struct
{
    UART_TypeDef *Instance;
    UART_InitTypeDef Init;
    volatile HAL_LockTypeDef Lock;
    volatile HAL_UART_StateTypeDef State;
    volatile uint32_t ErrorCode;
    /* First hardware RX line-error snapshot, preserved after FIFO cleanup.
     * Reading live LSR again would clear flags and lose the original status. */
    volatile uint32_t RxErrorLineStatus, RxErrorFifoLevel;
    DMA_HandleTypeDef *hdmatx, *hdmarx;
    const uint8_t *pTxBuffPtr;
    uint8_t *pRxBuffPtr;
    UART_TransferTypeDef Tx, Rx;
    uint32_t XferTimeout;
} UART_HandleTypeDef;

/* Module-wide polling deadline, shared by all instances and directions.
 * Default: HAL_POLLING_DEFAULT_TIMEOUT_MS (100 ms). 0 checks readiness without
 * waiting; HAL_MAX_DELAY waits indefinitely. Set in CPU0 foreground; Init and
 * DeInit preserve it. Each polling call snapshots the value once. */
void HAL_UART_SetPollingTimeout(uint32_t timeout);
HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_UART_DeInit(UART_HandleTypeDef *handle);
/* Nonblocking FIFO query; does not consume data or clear LSR errors.
 * Returns zero for an invalid/reset handle. */
uint32_t HAL_UART_GetRxFifoLevel(const UART_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *handle, const uint8_t *data, uint32_t size);
HAL_StatusTypeDef HAL_UART_Receive(UART_HandleTypeDef *handle, uint8_t *data, uint32_t size);
/* Wait for both FIFO and shift register to empty, using the polling timeout.
 * Call after async TX completion before changing baud/pins or disabling UART.
 * Returns HAL_BUSY if a TX transfer still owns the handle. */
HAL_StatusTypeDef HAL_UART_FlushTx(UART_HandleTypeDef *handle);
/* Fixed-size asynchronous transfers. TX and RX may run concurrently, using
 * different DMA channels. Buffers/linked DMA handles must remain valid until
 * completion or a successful abort. UART/DMA IRQs complete transfers without
 * Process; callbacks run after unlocking, normally in IRQ context. Keep them
 * short and nonblocking. Abort/Process and opposite-direction polling cleanup
 * may also invoke callbacks in the caller's context; rearming is supported.
 * TX completion means all bytes copied AND FIFO empty; the final character
 * may still be shifting out. FlushTx waits for wire completion if needed.
 * No idle/circular RX. */
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *handle, const uint8_t *data,
                                       uint32_t size);
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *handle, uint8_t *data, uint32_t size);
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *handle, const uint8_t *data,
                                        uint32_t size);
HAL_StatusTypeDef HAL_UART_Receive_DMA(UART_HandleTypeDef *handle, uint8_t *data, uint32_t size);
/* Optional Process-based deadline, default 100 ms after Init. Without Process
 * no software timeout is enforced. HAL_MAX_DELAY disables it. Change only
 * while both directions are idle; each direction has its own start time. */
HAL_StatusTypeDef HAL_UART_SetAsyncTimeout(UART_HandleTypeDef *handle, uint32_t timeout);
HAL_StatusTypeDef HAL_UART_Abort(UART_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *handle);
/* Optional foreground timeout check / retry of a DMA stop that timed out.
 * Not required for normal IT/DMA completion. No timer IRQ is reserved. */
void HAL_UART_Process(UART_HandleTypeDef *handle);
void HAL_UART_IRQHandler(UART_HandleTypeDef *handle);
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *handle);
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *handle);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *handle);
/* Optional polling text output. Explicit handle; no UART/pin initialization.
 * One lock and module polling deadline per call. Hex is eight uppercase digits without 0x.
 * PrintValue appends CRLF; HexDump writes spaced bytes followed by CRLF. */
HAL_StatusTypeDef HAL_UART_PrintString(UART_HandleTypeDef *handle, const char *text);
HAL_StatusTypeDef HAL_UART_PrintU32(UART_HandleTypeDef *handle, uint32_t value);
HAL_StatusTypeDef HAL_UART_PrintHex(UART_HandleTypeDef *handle, uint32_t value);
HAL_StatusTypeDef HAL_UART_PrintValue(UART_HandleTypeDef *handle, const char *label,
                                      uint32_t value);
HAL_StatusTypeDef HAL_UART_HexDump(UART_HandleTypeDef *handle, const uint8_t *data, uint32_t size);
void HAL_UART_MspInit(UART_HandleTypeDef *handle);
void HAL_UART_MspDeInit(UART_HandleTypeDef *handle);
#endif
