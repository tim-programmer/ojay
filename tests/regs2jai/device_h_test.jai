// Uses the output of: regs2jai.py device.h
// REGS2JAI: -- -I include -DSECURE
#import "Basic";
#load "device_h.jai";

offset :: (p: *void, base: *void) -> int { return cast(int) p - cast(int) base; }

main :: () {
    print("TIMER0 %\n", cast(int) TIMER0 == 0x5000_2000);   // the SECURE address
    print("TIMER1 %\n", cast(int) TIMER1 == 0x4000_3000);
    print("UART0 %\n", cast(int) UART0 == 0x4000_4000);
    print("MODE_OUT % MODE_IN %\n", offset(*TIMER0.MODE_OUT, TIMER0), offset(*TIMER0.MODE_IN, TIMER0));
    print("CH[1].FLAGS % STAMP %\n", offset(*TIMER0.CH[1].FLAGS, TIMER0), offset(*TIMER0.STAMP, TIMER0));
    print("size % %\n", size_of(TIMER_Type), size_of(UART_Type));
    print("IRQ % % % %\n", HardFault_IRQn, UART0_IRQn, UART1_IRQn, TIMER_IRQn);
    print("EN(1) % DIV(3) % FLAGS_HI(0x12) %\n", TIMER_CTRL_EN(1), TIMER_CTRL_DIV(3), TIMER_CH_FLAGS_HI(0x12));
    print("CLOCK_HZ % NEG % ALL_ONES % RXNE %\n", CLOCK_HZ, NEG, ALL_ONES, UART_STAT_RXNE_MASK);
}
