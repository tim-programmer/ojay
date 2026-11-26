// Short lambdas: `(params) => expression`. Untyped parameters take their
// types from the procedure type the lambda is used as; without `-> T` the
// results are that type's, or the expression's type.
#import "Basic";

double :: (x: int) => x * 2;
halve :: (x: float) -> float => x / 2;
announce :: () => print("side effect\n");
pair :: (a: int) -> int, int { return a, a + 1; }
both :: (a: int) => pair(a);   // several results

apply :: (f: (int) -> int, x: int) -> int { return f(x); }
apply2 :: (f: (int, int) -> int, a: int, b: int) -> int { return f(a, b); }
each :: (xs: []int, f: (int)) { for xs  f(it); }

map :: (xs: []$T, f: (T) -> $R) -> [..] R {
    out: [..] R;
    for xs  array_add(*out, f(it));
    return out;
}
reduce :: (xs: []$T, start: $R, f: (R, T) -> R) -> R {
    acc := start;
    for xs  acc = f(acc, it);
    return acc;
}

COMPILE_TIME :: #run reduce(int.[1, 2, 3], 0, (acc, x) => acc + x);

main :: () {
    print("% %\n", double(21), halve(3));
    announce();
    a, b := both(5);
    print("% %\n", a, b);

    print("%\n", apply((x) => x * x, 7));
    print("%\n", apply2((a, b) => a - b, 10, 3));
    print("%\n", apply((x: int) => x + 100, 1));
    f: (int) -> int = (n) => n * 3;
    print("%\n", f(5));
    each(int.[1, 2, 3], (x) => print("<%>", x));
    print("\n");
    g := (s: string) => s.count;
    print("% %\n", g("four"), type_of(g));
    callback: (s32) -> s32 #c_call = (x) => x + 1;   // takes #c_call from the type
    print("%\n", callback(1));

    print("%\n", map(int.[1, 2, 3], (x) => x * 10));
    print("%\n", map(string.["a", "bcd"], (s) => s.count));
    print("%\n", map(float.[1, 2], (x) => x * 0.5));
    print("%\n", map(int.[1, 2], (x: int) -> float => cast(float) x / 4));
    print("%\n", reduce(int.[1, 2, 3, 4], 0, (acc, x) => acc + x * x));
    print("%\n", COMPILE_TIME);
}
