#define TIMER_CH_COUNT 2
typedef struct {
  volatile uint32_t DATA;
  volatile const uint32_t STAT;
} UART_Type;
#define UART0 ((UART_Type *)0x40004000u)
#define UART_STAT_RXNE_MASK  0x20u
