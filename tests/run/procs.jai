#import "Basic";

fib :: (n: int) -> int {
    if n < 2 return n;
    return fib(n - 1) + fib(n - 2);
}

greet :: (name: string, greeting := "Hello", times := 1) {
    for 1..times print("%, %!\n", greeting, name);
}

min_max :: (xs: []int) -> (lo: int, hi: int) {
    lo := xs[0];
    hi := xs[0];
    for xs {
        if it < lo lo = it;
        if it > hi hi = it;
    }
    return lo, hi;
}

sum_all :: (nums: ..int) -> int {
    total := 0;
    for nums total += it;
    return total;
}

apply :: (f: (int) -> int, x: int) -> int { return f(x); }

double :: (x: int) -> int { return x * 2; }

// Declared after use: order independence at file scope.
main :: () {
    print("fib(20) = %\n", fib(20));
    greet("Sailor");
    greet("World", "Hi", 2);
    greet("you", times = 1, greeting = "Hey");

    lo, hi := min_max(int.[5, 3, 9, 1, 7]);
    print("lo = %, hi = %\n", lo, hi);
    first := min_max(int.[4, 8]);
    print("first = %\n", first);

    print("sum_all = % %\n", sum_all(1, 2, 3, 4), sum_all());

    print("apply = %\n", apply(double, 21));

    square :: (x: int) -> int { return x * x; }
    print("apply local = %\n", apply(square, 9));

    f := double;
    print("via variable = %\n", f(5));

    anon := (a: int) -> int { return a + 1000; };
    print("anon = %\n", anon(1));

    print("later = %\n", later());
}

later :: () -> int { return CONST * 2; }
CONST :: 21;
