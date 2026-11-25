#import "Basic";

Vector2 :: struct {
    x: float;
    y: float = 1;
}

Color :: enum u8 { RED; GREEN; BLUE :: 10; }

square :: (x: int) -> int { return x * x; }

divmod :: (a: int, b: int) -> int, int {
    return a / b, a % b;
}

main :: () {
    print("Hello, Sailor!\n");
    x := 5;
    print("square(%) = %\n", x, square(x));
    q, r := divmod(17, 5);
    print("17 / 5 = % rem %\n", q, r);
    v: Vector2;
    v.x = 3.5;
    print("v = %\n", v);
    c := Color.BLUE;
    print("color = %, as int = %\n", c, cast(int) c);
    sum := 0;
    for 1..10 sum += it;
    print("sum 1..10 = %\n", sum);
    arr := int.[1, 2, 3, 4];
    for arr print("arr[%] = %\n", it_index, it);
    print("arr = %\n", arr);
    name := "Jai";
    print("name has % bytes, pi ~ %\n", name.count, 3.14159);
    defer print("deferred!\n");
    if x > 3 then print("x > 3\n"); else print("x <= 3\n");
}
