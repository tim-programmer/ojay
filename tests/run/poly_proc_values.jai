#import "Basic";

identity :: (x: $T) -> T { return x; }
square :: (x: $T) -> T { return x * x; }
pair_sum :: (a: $T, b: T) -> T { return a + b; }
to_float :: (x: $T) -> $R { return cast(R) x; }
first :: (a: [] $T) -> T { return a[0]; }

apply :: (f: (int) -> int, x: int) -> int { return f(x); }
apply_f :: (f: (float) -> float, x: float) -> float { return f(x); }
map :: (xs: [] $T, f: (T) -> T) -> [..] T {
    out: [..] T;
    for xs  array_add(*out, f(it));
    return out;
}

main :: () {
    f: (int) -> int = identity;
    g: (float64) -> float64 = square;
    h: (s16, s16) -> s16 = pair_sum;
    k: (int) -> float32 = to_float;
    print("% % % %\n", f(3), g(1.5), h(2, 5), k(7));

    print("% %\n", apply(square, 9), apply_f(square, 0.5));
    print("%\n", map(int.[1, 2, 3], square));

    fs: [2] (int) -> int;
    fs[0] = identity;
    fs[1] = square;
    for fs  print("% ", it(4));
    print("\n");

    pick: ([] string) -> string = first;
    print("%\n", pick(.["a", "b"]));
    print("%\n", f == cast((int) -> int) identity);
}
