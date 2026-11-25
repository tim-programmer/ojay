#import "Basic";
#import "Math";

main :: () {
    f: float = 3.75;
    i := cast(int) f;
    print("cast float->int: %\n", i);
    g := cast(float64) i / 2;
    print("cast int->float64: %\n", g);
    big: s64 = 300;
    small := cast,trunc(u8) big;
    print("trunc: %\n", small);
    n: s32 = 12;
    m: s64 = n;   // implicit widening
    print("widen: %\n", m);
    u: u8 = 200;
    w: int = xx u;
    print("xx: %\n", w);
    print("sqrt: %\n", sqrt(2.0));
    print("floor/ceil: % %\n", floor(2.5), ceil(2.5));
    print("min/max/abs: % % %\n", min(3, 9), max(-2.5, 1.0), abs(-17));
    print("PI: %\n", cast(float64) PI);
    print("limits: % %\n", S32_MAX, U8_MAX);
    d: float64 = 1e20;
    print("big float: %\n", d);
    print("neg float: %\n", -0.125);
}
