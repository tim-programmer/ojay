// Uses the output of: regs2jai.py device.svd
#import "Basic";
#load "device_svd.jai";

offset :: (p: *void, base: *void) -> int { return cast(int) p - cast(int) base; }

main :: () {
    print("GPIOB at %\n", cast(int) GPIOB == 0x4001_1000);
    print("same type %\n", type_of(GPIOA) == type_of(GPIOB));
    print("OUT %\n", offset(*GPIOA.OUT, GPIOA));
    print("CTRL_A % CTRL_B % CTRL_HI %\n", offset(*GPIOA.CTRL_A, GPIOA), offset(*GPIOA.CTRL_B, GPIOA),
          offset(*GPIOA.CTRL_HI, GPIOA));
    print("PCR[5] %\n", offset(*GPIOA.PCR[5], GPIOA));
    print("FILT_LO % FILT_HI %\n", offset(*GPIOA.FILT_LO, GPIOA), offset(*GPIOA.FILT_HI, GPIOA));
    print("CH[2].DATA %\n", offset(*GPIOA.CH[2].DATA, GPIOA));
    print("size %\n", size_of(GPIO_Type));
    print("TIMER COUNT % if %\n", offset(*TIMER.COUNT, TIMER), offset(*TIMER.if_, TIMER));
    print("PIN3 mask % shift % output %\n", GPIO_MODE_PIN3_MASK, GPIO_MODE_PIN3_SHIFT, GPIO_MODE_PIN3_OUTPUT);
    print("PIN2(OUTPUT) %\n", GPIO_MODE_PIN2(GPIO_MODE_PIN2_OUTPUT));
    print("OUT VALUE(0x123) % EN %\n", GPIO_OUT_VALUE(0x123), GPIO_OUT_EN(1));
    print("CTRL_HI X(0xAB) %\n", GPIO_CTRL_HI_X(0xAB));
    print("IRQ % %\n", GPIOA_IRQn, GPIOB_IRQn);
}
