/* A made-up CMSIS-style device header covering what tools/regs2jai.py handles. */
#ifndef DEVICE_H_
#define DEVICE_H_

#include <stdint.h>
#include "periph_uart.h"   /* found through -I include */

#define __IO volatile
#define __I  volatile const

typedef enum IRQn {
  HardFault_IRQn = -13,
  SysTick_IRQn   = -1,
  UART0_IRQn     = 0,
  UART1_IRQn,
  TIMER_IRQn     = UART1_IRQn + 4,
} IRQn_Type;

typedef struct {
  __IO uint32_t CTRL;
       uint8_t RESERVED_0[4];
  union {
    __IO uint32_t MODE_OUT;
    __I  uint32_t MODE_IN;
  };
  struct {
    __IO uint16_t VAL;
    __IO uint16_t FLAGS;
         uint8_t RESERVED_0[4];
  } CH[TIMER_CH_COUNT];
  __I uint64_t STAMP;
} TIMER_Type;

typedef struct {               /* bitfields have no Jai equivalent: skipped */
  uint32_t ISR:9;
  uint32_t _reserved0:23;
} IPSR_Bits;

static inline void enable(void) { __asm volatile ("cpsie i" : : : "memory"); }

#define TIMER_CTRL_EN_MASK    (0x1U)
#define TIMER_CTRL_EN_SHIFT   (0U)
#define TIMER_CTRL_EN(x)      (((uint32_t)(((uint32_t)(x)) << TIMER_CTRL_EN_SHIFT)) & TIMER_CTRL_EN_MASK)
#define TIMER_CTRL_DIV_MASK   (0xF0U)
#define TIMER_CTRL_DIV_SHIFT  (4U)
#define TIMER_CTRL_DIV(x)     (((uint8_t)(((uint8_t)(x)) << TIMER_CTRL_DIV_SHIFT)) & TIMER_CTRL_DIV_MASK)
#define TIMER_CH_FLAGS_HI_MASK  (0xFF0000U)
#define TIMER_CH_FLAGS_HI_SHIFT (16U)
#define TIMER_CH_FLAGS_HI(x)  (((uint16_t)(((uint16_t)(x)) << TIMER_CH_FLAGS_HI_SHIFT)) & TIMER_CH_FLAGS_HI_MASK)

#define PERIPH_BASE           (0x40000000UL)
#if defined(SECURE)
#define TIMER0_BASE           (PERIPH_BASE + 0x10000000u + 0x2000u)
#else
#define TIMER0_BASE           (PERIPH_BASE + 0x2000u)
#endif
#define TIMER0                ((TIMER_Type *)TIMER0_BASE)
#define TIMER1                ((TIMER_Type *) (0x40003000u))
#define TIMER_BASE_PTRS       { TIMER0, TIMER1 }
#define CLOCK_HZ              (48000000u / (1 << 2))
#define NEG                   (-(int32_t)5)
#define ALL_ONES              ((uint32_t)~0u)
#define NOT_A_CONSTANT        some_function()
#define STRING                "hello"

#endif
