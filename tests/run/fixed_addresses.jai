// Pointers to fixed addresses are compile-time constants, as for memory-mapped
// register blocks: `GPIO0 :: cast(*GPIO_Type) 0x4009_6000;`.
#import "Basic";

GPIO_Type :: struct {
    VERID: u32;
    _reserved0: [0x3C]u8;
    PDOR: u32;
    PCR: [32]u32;
}

GPIO0_BASE :: 0x4009_6000;
GPIO0 :: cast(*GPIO_Type) GPIO0_BASE;
GPIO1 : *GPIO_Type : xx 0x4009_8000;
AS_INT :: cast(int) GPIO1;
#assert AS_INT == 0x4009_8000;

global_pointer := GPIO0;   // a constant address is a valid static initializer

addr :: (p: *void) -> int { return cast(int) p; }

main :: () {
    GPIO0_PCR := cast(*u32) *GPIO0.PCR;   // an address computed from a constant one
    print("%\n", addr(GPIO0) == GPIO0_BASE);
    print("%\n", addr(*GPIO0.PDOR) - GPIO0_BASE);
    print("%\n", addr(*GPIO1.PCR[3]) - addr(GPIO1));
    print("%\n", addr(GPIO0_PCR) - GPIO0_BASE);
    print("%\n", addr(global_pointer) == GPIO0_BASE);
    print("%\n", global_pointer != null);
}
