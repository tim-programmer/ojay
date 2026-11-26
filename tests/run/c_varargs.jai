// C varargs: #foreign procedures whose last parameter is `..` (C side:
// c_varargs.c). Arguments get C's default promotions.
#import "Basic";

sum_ints :: (count: s32, values: ..) -> s64 #foreign;
describe :: (out: *u8, kinds: *u8, args: ..) -> s32 #foreign;
mix :: (n: s32, args: ..Any) -> float64 #foreign;

Small :: enum u8 { A :: 7; B :: 9; }

main :: () {
    print("%\n", sum_ints(0));
    print("%\n", sum_ints(3, 1, 2, 3));
    a: u8 = 200;
    b: s16 = -300;
    print("%\n", sum_ints(4, a, b, true, Small.B));

    buffer: [200] u8;
    x: s32 = 42;
    f: float32 = 1.5;
    big: s64 = 1 << 40;
    n := describe(buffer.data, "iulddps".data, -5, cast(u32) 4000000000, big, f, 2.25, *x, "text".data);
    print("% [%]\n", n, to_string(buffer.data));

    print("%\n", mix(6, 0.5, 1, 1.5, 2, 2.5, 3, 3.5, 4, 4.5, 5, 5.5, 6));
}
