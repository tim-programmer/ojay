#import "Basic";

Point :: struct { x := 1; y := 2; }
Plain :: struct { a: int; b: float; }
Nested :: struct { p: Point; tag := "hi"; }

INIT_AT_COMPILE_TIME :: #run () -> int {
    init := initializer_of(Point);
    p: Point = ---;
    init(*p);
    return p.x * 10 + p.y;
}();

main :: () {
    p: Point;
    p.x = 100;
    p.y = 200;
    init := initializer_of(Point);
    init(*p);
    print("%\n", p);

    print("%\n", initializer_of(Plain) == null);
    print("%\n", initializer_of(int) == null);

    n: Nested = ---;
    initializer_of(Nested)(*n);
    print("%\n", n);

    arr: [2] Point = ---;
    initializer_of([2] Point)(*arr);
    print("%\n", arr);

    print("%\n", INIT_AT_COMPILE_TIME);
    print("%\n", initializer_of(Point) == init);
}
