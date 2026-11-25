#import "Basic";

Vec2 :: struct { x, y: float; }
Shape :: enum { CIRCLE; SQUARE; }

describe :: (x: int) -> string { return "int"; }
describe :: (x: float) -> string { return "float"; }
describe :: (x: string) -> string { return "string"; }
describe :: (v: Vec2) -> string { return "Vec2"; }
describe :: (s: Shape) -> string { return "Shape"; }
describe :: (a: int, b: int) -> string { return "two ints"; }
describe :: (p: *Vec2) -> string { return "pointer to Vec2"; }

area :: (radius: float) -> float { return 3 * radius * radius; }
area :: (w: float, h: float, scale := 1.0) -> float { return w * h * scale; }

// A user procedure with a built-in's name overloads the built-in.
max :: (a: Vec2, b: Vec2) -> Vec2 { return .{max(a.x, b.x), max(a.y, b.y)}; }

total :: (values: ..int) -> int {
    sum := 0;
    for values sum += it;
    return sum;
}
total :: (a: int) -> int { return a * 100; }   // preferred over the variadic one for one argument

main :: () {
    v := Vec2.{1, 2};
    small: u8 = 3;
    print("%\n", describe(5));
    print("%\n", describe(small));          // u8 converts to int
    print("%\n", describe(2.5));
    print("%\n", describe("hi"));
    print("%\n", describe(v));
    print("%\n", describe(.{3, 4}));        // only Vec2 takes a struct literal
    print("%\n", describe(.SQUARE));        // only Shape has a member SQUARE
    print("%\n", describe(1, 2));
    print("%\n", describe(*v));
    print("% % %\n", area(2), area(2, 3), area(w = 2, h = 3, scale = 2));
    m := max(v, Vec2.{5, 0});
    print("% % %\n", m.x, m.y, max(3, 9));
    print("% %\n", total(1, 2, 3), total(4));

    // Overloads declared inside a procedure.
    twice :: (x: int) -> int { return x * 2; }
    twice :: (s: string) -> string { return sprint("%+%", s, s); }
    print("% %\n", twice(21), twice("ab"));

    // A procedure value picks the overload by its type.
    f: (float) -> string = describe;
    print("%\n", f(1));
}
