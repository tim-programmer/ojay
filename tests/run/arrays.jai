#import "Basic";

sum :: (xs: []int) -> int {
    s := 0;
    for xs s += it;
    return s;
}

main :: () {
    a: [5]int;
    for 0..a.count-1 a[it] = it * it;
    print("a = %, count = %\n", a, a.count);
    print("sum = %\n", sum(a));

    b := int.[10, 20, 30];
    print("sum b = %\n", sum(b));

    // modify through pointer iteration
    for * b { it.* += 1; }
    print("b = %\n", b);

    for x, i: b print("b[%]=% ", i, x);
    print("\n");

    // dynamic arrays
    d: [..]int;
    for 1..10 array_add(*d, it * 10);
    print("d.count = %, d[9] = %\n", d.count, d[9]);
    print("d = %\n", d);
    v: []int = d;
    print("view sum = %\n", sum(v));
    array_reset(*d);
    print("after reset: %\n", d.count);

    // strings
    s := "hello";
    print("s[1] = %, count = %\n", s[1], s.count);
    print("eq: % %\n", s == "hello", s == "world");

    // 2d
    grid: [3][3]int;
    for i: 0..2 for j: 0..2 grid[i][j] = i * 3 + j;
    print("grid = %\n", grid);

    // array of structs
    Pair :: struct { a: int; b: string; }
    pairs := Pair.[.{1, "one"}, .{2, "two"}];
    for pairs print("% -> %\n", it.a, it.b);

    words := NewArray(3, string);
    words[0] = "x"; words[1] = "y"; words[2] = "z";
    print("words = %\n", words);
}
