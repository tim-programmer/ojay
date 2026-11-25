#import "Basic";

// Mutual recursion, procedure tables, struct returns, varargs of structs.

is_even :: (n: int) -> bool { if n == 0 return true;  return is_odd(n - 1); }
is_odd  :: (n: int) -> bool { if n == 0 return false; return is_even(n - 1); }

Op :: struct {
    name: string;
    apply: (int, int) -> int;
}

add :: (a: int, b: int) -> int { return a + b; }
sub :: (a: int, b: int) -> int { return a - b; }
mul :: (a: int, b: int) -> int { return a * b; }

Vec2 :: struct { x, y: float; }

midpoint :: (points: ..Vec2) -> Vec2 {
    sum: Vec2;
    for points {
        sum.x += it.x;
        sum.y += it.y;
    }
    if points.count == 0 return sum;
    n := cast(float) points.count;
    return .{sum.x / n, sum.y / n};
}

stats :: (xs: []float64) -> (mean: float64, lo: float64, hi: float64) {
    total: float64 = 0;
    lo := xs[0];
    hi := xs[0];
    for xs {
        total += it;
        if it < lo lo = it;
        if it > hi hi = it;
    }
    return total / cast(float64) xs.count, lo, hi;
}

clamp :: (x: int, lo := 0, hi := 100) -> int {
    return ifx x < lo then lo else ifx x > hi then hi else x;
}

counter_value: int;
bump :: (by := 1) -> int {
    counter_value += by;
    return counter_value;
}

main :: () {
    print("even(10)=% odd(7)=% even(7)=%\n", is_even(10), is_odd(7), is_even(7));

    ops := Op.[.{"add", add}, .{"sub", sub}, .{"mul", mul}];
    for ops print("% 6 3 = %\n", it.name, it.apply(6, 3));

    m := midpoint(.{0, 0}, .{4, 0}, .{4, 2}, .{0, 2});
    print("midpoint = %\n", m);
    print("empty midpoint = %\n", midpoint());

    mean, lo, hi := stats(float64.[2.5, 9, -1, 4.5]);
    print("mean=% lo=% hi=%\n", mean, lo, hi);

    print("clamp: % % % %\n", clamp(-5), clamp(50), clamp(500), clamp(500, hi = 1000));

    // Arguments are evaluated left to right.
    print("bumps: % % %\n", bump(), bump(10), bump());

    // A local procedure used as a value.
    twice :: (f: (int) -> int, x: int) -> int { return f(f(x)); }
    inc :: (x: int) -> int { return x + 1; }
    sq  :: (x: int) -> int { return x * x; }
    print("twice: % %\n", twice(inc, 5), twice(sq, 3));

    // Procedure pointers can be null-checked and compared.
    f: (int) -> int;
    print("f is null: %\n", f == null);
    f = sq;
    print("f(7)=%, f==sq: %, f==inc: %\n", f(7), f == sq, f == inc);
}
