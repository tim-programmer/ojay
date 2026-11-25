#import "Basic";

// Integer widths, signedness, wrapping and conversions.

main :: () {
    a: s8 = 127;
    a += 1;
    b: u8 = 0;
    b -= 1;
    print("s8 wrap: %, u8 wrap: %\n", a, b);

    c: s16 = -32768;
    d: u16 = 65535;
    print("s16: %, u16: %\n", c, d);

    // Signed division truncates toward zero; % takes the sign of the dividend.
    print("div: % % % %\n", 7 / 2, -7 / 2, 7 / -2, -7 / -2);
    print("mod: % % % %\n", 7 % 3, -7 % 3, 7 % -3, -7 % -3);

    // Shifts: arithmetic for signed, logical for unsigned.
    sn: s32 = -16;
    un: u32 = 0xF000_0000;
    print("shifts: % % % %\n", sn >> 2, un >> 4, sn << 3, cast(u32) 1 << 31);

    x: u32 = 0xDEADBEEF;
    print("bitwise: % % % %\n", x & 0xFFFF, x | 0xF, x ^ 0xFFFF_FFFF, ~x);

    big: u64 = 0xFFFF_FFFF_FFFF_FFFF;
    print("u64 max: %, +1 wraps to %\n", big, big + 1);
    m: s64 = 3_037_000_499;
    print("square: %\n", m * m);

    // Conversions
    f := 3.99;
    print("float->int: % % %\n", cast(int) f, cast(int) -f, cast(u8) 255.9);
    i := 1234567;
    print("int->float32: %, int->float64: %\n", cast(float32) i, cast(float64) i);
    neg: s32 = -1;
    print("s32 -1 as u32: %, as s64: %, as u64: %\n", cast(u32) neg, cast(s64) neg, cast(u64) cast(s64) neg);
    print("trunc: % %\n", cast,trunc(u8) 0x1234, cast,trunc(s8) 200);

    // Float behaviour
    third: float32 = 1.0 / 3.0;
    third64: float64 = 1.0 / 3.0;
    print("1/3: % %\n", third, third64);
    print("float ops: % % % %\n", 0.1 + 0.2, 2.5 * 4, 10.0 / 4, -0.0);
    tiny: float64 = 0.000001;
    print("tiny: %, huge: %\n", tiny, 123456789.0 * 1000.0);
    print("compare: % % %\n", 0.1 + 0.2 == 0.3, 1.5 < 2.5, cast(float64) 0.1 == 0.1);

    // Mixed widths widen implicitly.
    small: u8 = 200;
    wide: s32 = 1000;
    print("mixed: %\n", small + wide);

    // Bool <-> int
    t := true;
    print("bool as int: % %\n", cast(int) t, cast(int) !t);
}
