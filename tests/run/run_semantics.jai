// The compile-time interpreter must compute exactly what the compiled program
// computes, on every target: each procedure here runs both in #run and at run
// time, and the results are compared.
#import "Basic";
#import "Math";

Flags :: enum_flags u8 { A; B; C; }
Shape :: enum s16 { CIRCLE :: -1; SQUARE :: 300; }
Pair :: struct { a: s8; b: u16; }

divmod :: (a: int, b: int) -> int, int { return a / b, a % b; }
apply :: (f: (int) -> int, x: int) -> int { return f(x); }
square :: (x: int) -> int { return x * x; }

integers :: () -> [24]int {
    r: [24]int;
    a: u8 = 250;
    a += 10;                                  // wraps to 4
    r[0] = a;
    b: s8 = 127;
    b += 1;                                   // wraps to -128
    r[1] = b;
    c: u32 = 0xFFFF_FFFF;
    r[2] = c * c;                             // u32 multiply wraps
    r[3] = -7 / 2;                            // truncates toward zero
    r[4] = -7 % 3;
    r[5] = (cast(s32) -16) >> 2;              // arithmetic shift
    r[6] = (cast(u32) 0x8000_0000) >> 31;     // logical shift
    wide: u16 = 0x1FF;
    r[7] = cast(int) (cast(u8) wide);         // truncates
    r[8] = cast(int) cast(s16) cast(u16) 0xFFFE;
    q, m := divmod(-17, 5);
    r[9] = q * 100 + m;
    r[10] = apply(square, 9);
    f := Flags.A | .C;
    r[11] = cast(int) f;
    r[12] = cast(int) Shape.SQUARE + cast(int) Shape.CIRCLE;
    p := Pair.{-3, 65535};
    r[13] = cast(int) p.a * cast(int) p.b;
    r[14] = ~cast(u16) 5;
    r[15] = cast(int) (cast(s64) 0x7FFF_FFFF_FFFF_FFFF + 1 < 0);
    x := 0;
    for < i: 1..5 { x = x * 10 + i; }         // reverse range
    r[16] = x;
    arr := int.[5, 6, 7];
    for * arr  it.* *= 2;
    r[17] = arr[0] + arr[1] + arr[2];
    k := 0;
    for i: 0..9 { if i == 2 { i = 7; } k += 1; }   // writing `it` changes the iteration
    r[18] = k;
    r[19] = cast(int) max(3, -4) + min(3, -4) + abs(-12);
    order := 0;
    {
        defer order = order * 10 + 1;
        defer order = order * 10 + 2;
        order = 3;
    }
    r[20] = order;                            // 321: defers run in reverse
    s := "hello";
    if s == {
        case "help"; r[21] = 1;
        case "hello"; r[21] = 2; #through;
        case "x"; r[21] += 10;
        case; r[21] = 99;
    }
    dyn: [..]s16;
    for 1..20  array_add(*dyn, cast(s16) (it * it));
    r[22] = dyn.count * 1000 + dyn[19];
    bytes: [8]u8;
    memset(bytes.data, 0xAB, 8);
    memcpy(*bytes[4], *r[2], 2);
    r[23] = cast(int) bytes[3] + cast(int) bytes[4] * 256 + cast(int) bytes[5];
    return r;
}

floats :: () -> [8]float64 {
    r: [8]float64;
    a: float32 = 16777216.0;
    a += 1.0;                                 // float32 rounding: stays 16777216
    r[0] = a;
    r[1] = cast(float64) (cast(float32) 0.1);
    r[2] = sqrt(2.0);
    r[3] = floor(-2.5) + ceil(2.1) + round(2.5);
    r[4] = cast(float64) cast(int) -3.99;
    r[5] = pow(2.0, 10.0);
    r[6] = cast(float64) (1.0 / 3.0);
    r[7] = cast(float) (cast(u64) 0xFFFF_FFFF_FFFF_FFFF);
    return r;
}

text :: () -> string {
    names: [..]string;
    array_add(*names, "a");
    array_add(*names, "bc");
    v: Pair;
    v.b = 7;
    return sprint("% % % % % % % %", names, v, Flags.A | .B, Shape.SQUARE, 1.5, -0.25, u8, cast(s8) -5);
}

INTEGERS :: #run integers();
FLOATS :: #run floats();
TEXT :: #run text();

main :: () {
    ri := integers();
    for INTEGERS  if it != ri[it_index]  print("integer % differs: compile time %, run time %\n", it_index, it, ri[it_index]);
    rf := floats();
    for FLOATS  if it != rf[it_index]  print("float % differs: compile time %, run time %\n", it_index, it, rf[it_index]);
    if TEXT != text()  print("text differs: compile time '%', run time '%'\n", TEXT, text());
    print("%\n", INTEGERS);
    print("%\n", FLOATS);
    print("%\n", TEXT);
}
