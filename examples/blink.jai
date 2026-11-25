// Blink the red user LED on an NXP FRDM-MCXN947 board.
//
//   ojay-arm32 -O2 blink.jai -o blink.o
//   (link with mcxn947-flash.ld, see examples/Makefile and README.md)
//
// The red LED of the board's RGB LED is on pin P0_10 and is active low.
// Register addresses come from NXP's MCUXpresso SDK headers for the MCX N947;
// the core runs at 48 MHz out of reset.

#import "Basic";
#import "Hardware";   // ojay extension: volatile_load / volatile_store

// System configuration: clock gates and peripheral resets.
SYSCON_BASE    :: 0x4000_0000;
PRESETCTRL0    :: SYSCON_BASE + 0x100;   // 1 = held in reset
PRESETCTRLCLR0 :: SYSCON_BASE + 0x140;   // write 1 to release from reset
AHBCLKCTRLSET0 :: SYSCON_BASE + 0x220;   // write 1 to enable a clock

CLOCK_PORT0 :: 1 << 13;
CLOCK_GPIO0 :: 1 << 19;

// Pin multiplexing for port 0: one Pin Control Register per pin.
PORT0_BASE :: 0x4011_6000;
PORT_PCR   :: 0x80;

// GPIO port 0.
GPIO0_BASE :: 0x4009_6000;
GPIO_PSOR  :: 0x44;   // set output bits
GPIO_PTOR  :: 0x4C;   // toggle output bits
GPIO_PDDR  :: 0x54;   // direction: 1 = output

LED_RED_PIN :: 10;

// Cortex-M SysTick timer.
SYST_CSR :: 0xE000_E010;   // control and status
SYST_RVR :: 0xE000_E014;   // reload value
SYST_CVR :: 0xE000_E018;   // current value
SYST_COUNTFLAG :: 1 << 16;

CORE_CLOCK_HZ :: 48_000_000;

write :: (address: int, value: u32) { volatile_store(cast(*u32) address, value); }
read  :: (address: int) -> u32      { return volatile_load(cast(*u32) address); }

init_led :: () {
    clocks : u32 : CLOCK_PORT0 | CLOCK_GPIO0;   // reset bits use the same positions
    write(AHBCLKCTRLSET0, clocks);
    write(PRESETCTRLCLR0, clocks);
    while read(PRESETCTRL0) & clocks {}

    write(PORT0_BASE + PORT_PCR + 4 * LED_RED_PIN, 0);    // MUX 0 = GPIO
    write(GPIO0_BASE + GPIO_PSOR, 1 << LED_RED_PIN);      // start with the LED off
    write(GPIO0_BASE + GPIO_PDDR, read(GPIO0_BASE + GPIO_PDDR) | 1 << LED_RED_PIN);
}

init_timer :: () {
    write(SYST_RVR, CORE_CLOCK_HZ / 1000 - 1);   // wrap every millisecond
    write(SYST_CVR, 0);
    write(SYST_CSR, 0b101);                      // enable, core clock, no interrupt
}

delay_ms :: (ms: int) {
    // COUNTFLAG is set each time the counter wraps and cleared when read.
    for 1..ms  while !(read(SYST_CSR) & SYST_COUNTFLAG) {}
}

main :: () {
    init_led();
    init_timer();
    // Without a debugger this goes to the UART (the MCU-Link's virtual COM port).
    count := 0;
    while true {
        write(GPIO0_BASE + GPIO_PTOR, 1 << LED_RED_PIN);
        count += 1;
        if count % 2 == 0  print("blink %\n", count / 2);
        delay_ms(500);
    }
}
