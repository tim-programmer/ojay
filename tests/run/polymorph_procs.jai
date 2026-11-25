// Polymorphic procedures ($T): inference, explicit type parameters, baked
// compile-time values, overloading and modules.
#import "Basic";
#import "Containers";

// ---- inference
max_of :: (a: $T, b: T) -> T { return ifx a > b then a else b; }

first :: (items: []$T) -> T { return items[0]; }

last :: (items: [..]$T) -> T { return items[items.count - 1]; }

swap :: (a: *$T, b: *T) {
    t := a.*;
    a.* = b.*;
    b.* = t;
}

count_of :: (a: [$N]$T) -> int { return N; }

apply :: (x: $T, f: (T) -> $R) -> R { return f(x); }

describe_type :: (x: $T) -> string {
    return sprint("% (% bytes)", T, size_of(T));
}

sum_all :: (values: ..$T) -> T {
    s: T;
    for values s += it;
    return s;
}

// ---- explicit type parameters and baked values
make :: ($T: Type) -> *T { return New(T); }

zero_of :: ($T: Type) -> T {
    z: T;
    return z;
}

convert :: (x: int, $T: Type) -> T { return cast(T) x; }

repeat :: ($N: int, value: $T) -> [N]T {
    result: [N]T;
    for 0..N-1 result[it] = value;
    return result;
}

scaled :: (x: int, $FACTOR := 10) -> int { return x * FACTOR; }

// Two instances of one template with different baked values are independent.
nth_power :: (x: int, $P: int) -> int {
    #if P == 0 { return 1; }
    else { return x * nth_power(x, P - 1); }
}

// ---- overloading: a non-polymorphic procedure wins a tie
show :: (x: int) -> string { return "int overload"; }
show :: (x: $T) -> string { return sprint("polymorphic (%)", T); }
show :: (a: $T, b: T) -> string { return sprint("pair of %", T); }

// ---- a generic dynamic-array helper written in Jai
append :: (array: *[..]$T, value: T) {
    if array.count == array.allocated {
        n := ifx array.allocated < 8 then 8 else array.allocated * 2;
        array.data = realloc(array.data, n * size_of(T), array.allocated * size_of(T));
        array.allocated = n;
    }
    array.data[array.count] = value;
    array.count += 1;
}

index_of :: (array: []$T, value: T) -> int {
    for array if it == value return it_index;
    return -1;
}

map :: (array: []$T, f: (T) -> $R) -> [..]R {
    out: [..]R;
    for array append(*out, f(it));
    return out;
}

// Recursion: the instance calls itself (one instance per element type).
quicksort :: (a: []$T) {
    if a.count < 2 return;
    pivot := a[a.count / 2];
    i, j := 0, a.count - 1;
    while i <= j {
        while a[i] < pivot i += 1;
        while a[j] > pivot j -= 1;
        if i <= j {
            swap(*a[i], *a[j]);
            i += 1;
            j -= 1;
        }
    }
    left, right: []T;
    left.data, left.count = a.data, j + 1;
    right.data, right.count = a.data + i, a.count - i;
    quicksort(left);
    quicksort(right);
}

// The polymorphic candidate would not compile for a string, but the string
// overload wins, so that instance is never created or checked.
negate :: (x: $T) -> T { return -x; }
negate :: (s: string) -> string { return sprint("not %", s); }

double :: (x: int) -> int { return x * 2; }
len :: (s: string) -> int { return s.count; }
twice :: (x: float) -> float { return x * 2; }

main :: () {
    print("max_of: % % % %\n", max_of(3, 7), max_of(2.5, -1.0), max_of(cast(u8) 3, 200), max_of(-4, -9));
    a: s16 = 300;
    print("max_of s16: % %\n", max_of(a, 299), type_of(max_of(a, 1)));

    numbers := int.[4, 8, 15, 16, 23, 42];
    words: [..]string;
    array_add(*words, "alpha");
    array_add(*words, "beta");
    print("first: % % %\n", first(numbers), first(words), first(string.["x", "y"]));
    print("last: %\n", last(words));
    print("count_of: % %\n", count_of(numbers), count_of(float.[1, 2]));

    x, y := 1, 2;
    swap(*x, *y);
    s, t := "left", "right";
    swap(*s, *t);
    print("swap: % % % %\n", x, y, s, t);

    print("apply: % % %\n", apply(21, double), apply("four", len), apply(1.5, twice));
    print("describe_type: % | % | %\n", describe_type(1), describe_type(true), describe_type(numbers));
    print("sum_all: % % %\n", sum_all(1, 2, 3), sum_all(0.5, 0.25), sum_all(a, 10));

    p := make(float64);
    p.* = 2.5;
    print("make: % %\n", p.*, type_of(p));
    print("zero_of: % \"%\" %\n", zero_of(int), zero_of(string), zero_of(bool));
    print("convert: % %\n", convert(65, u8), convert(-1, u16));
    r := repeat(3, "ab");
    print("repeat: % % %\n", r, type_of(r), type_of(repeat(2, 7)));
    print("scaled: % %\n", scaled(4), scaled(4, 3));
    print("nth_power: % % %\n", nth_power(2, 10), nth_power(3, 3), nth_power(5, 0));

    small: s32 = 5;
    print("show: % | % | % | % | %\n", show(5), show(small), show("hi"), show(1.5, 2), show(numbers));

    // Untyped constants take their default type: T = int, then T = float32.
    print("defaults: % %\n", type_of(max_of(1, 2)), type_of(max_of(1, 2.5)));

    // Generic dynamic arrays written in Jai.
    squares: [..]int;
    for 1..10 append(*squares, it * it);
    print("append: % (count %, allocated %)\n", squares, squares.count, squares.allocated);
    print("index_of: % % %\n", index_of(squares, 49), index_of(squares, 50), index_of(words, "beta"));
    doubled := map(numbers, double);
    lengths := map(words, len);
    print("map: % % %\n", doubled, lengths, type_of(lengths));

    // From a module: instances are checked in the module's scope.
    st: Stack(int);
    for 1..5 push(*st, it * 10);
    names: Stack(string);
    push(*names, "first");
    push(*names, "second");
    print("stack: % % %, % %\n", pop(*st), pop(*st), st.items.count, pop(*names), names.items.count);
    pair := make_pair("answer", 42);
    print("pair: % % %\n", pair.first, pair.second, type_of(pair));

    unsorted := int.[5, 3, 9, 1, 7, 2, 8];
    quicksort(unsorted);
    floats := float.[2.5, -1, 0.5, 0];
    quicksort(floats);
    print("quicksort: % %\n", unsorted, floats);
    print("negate: % % %\n", negate(3), negate(1.5), negate("ok"));
    print("named: % %\n", repeat(value = 1.5, N = 2), convert(T = s8, x = 100));

    // Local polymorphic procedures.
    clamp :: (v: $T, lo: T, hi: T) -> T { return ifx v < lo then lo else ifx v > hi then hi else v; }
    print("clamp: % % %\n", clamp(15, 0, 10), clamp(-2.5, -1.0, 1.0), clamp(cast(u8) 7, 1, 9));
}
