#import "Basic";

main :: () {
    a := 10;
    b: int = 3;
    c: s32 = 7;
    d : u8 = 255;
    print("% % % %\n", a + b, a - b, a * b, a / b);
    print("% %\n", a % b, -a % b);
    print("% %\n", c * 2, d);
    d += 1;  // wraps
    print("d wrapped = %\n", d);
    f: float = 1.5;
    g := 2.25;          // float32
    h: float64 = 1.0 / 3.0;
    print("% % %\n", f * 2, g + f, h);
    print("% %\n", 7 / 2, 7.0 / 2);
    big: u64 = 0xFFFF_FFFF_FFFF_FFFF;
    print("big = %\n", big);
    neg: s64 = -9223372036854775807 - 1;
    print("neg = %\n", neg);
    print("bits: % % % %\n", 0b1010 | 0b0101, 12 & 10, 12 ^ 10, 1 << 10);
    x := 1000;
    print("shift: % %\n", x >> 3, x << 2);
    print("bool: % % %\n", true, !true, 3 > 2 && 2 > 1);
    print("100%% done\n");
}
