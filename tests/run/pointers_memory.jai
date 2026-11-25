#import "Basic";

swap :: (a: *int, b: *int) {
    t := a.*;
    a.* = b.*;
    <<b = t;
}

Point :: struct { x: int; y: int = 7; }

main :: () {
    x, y := 1, 2;
    swap(*x, *y);
    print("x=% y=%\n", x, y);

    p := New(Point);
    print("new point = %\n", p.*);
    p.x = 42;
    print("p.x = %\n", p.x);
    free(p);

    buf := cast(*u8) alloc(4);
    for 0..3 buf[it] = cast(u8) (65 + it);
    s: string;
    s.data = buf;
    s.count = 4;
    print("s = %\n", s);

    nums := int.[10, 20, 30];
    q := *nums[0];
    q += 1;
    print("q.* = %, diff = %\n", q.*, q - *nums[0]);

    np: *int = null;
    print("null? %\n", np == null);

    a, b := 3, 4;
    a, b = b, a;
    print("parallel swap: % %\n", a, b);

    v: *void = *x;
    ip := cast(*int) v;
    print("through void: %\n", ip.*);
}
