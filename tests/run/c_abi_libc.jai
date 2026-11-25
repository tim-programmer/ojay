// C library functions that return structs by value: div and lldiv.
// SKIP: static
#import "Basic";

libc :: #system_library "libc";

div_t :: struct { quot, rem: s32; }      // 8 bytes
lldiv_t :: struct { quot, rem: s64; }    // 16 bytes

div :: (numer: s32, denom: s32) -> div_t #foreign libc;
lldiv :: (numer: s64, denom: s64) -> lldiv_t #foreign libc;

main :: () {
    print("%\n", div(47, 5));
    print("%\n", div(-47, 5));
    print("%\n", lldiv(1 << 40, 1000));
    r := lldiv(-123456789012, 7);
    print("% %\n", r.quot, r.rem);
}
