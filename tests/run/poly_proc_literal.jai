#import "Basic";

apply :: (f: (float) -> float, x: float) -> float { return f(x); }

main :: () {
    f: (int) -> int = (x: $T) -> T { return x * 3; };
    g: (string, int) -> string = (s: $S, n: int) -> S { return tprint("%:%", s, n); };
    print("% %\n", f(5), g("a", 2));
    print("%\n", apply((x: $T) -> T { return x + 0.25; }, 1));
    h: (s8) -> s8 = (x: $T) => x - 1;
    print("% %\n", h(0), type_of(h));
}
