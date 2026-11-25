#import "Basic";

Vec2 :: struct { x, y: float; }

operator + :: (a: Vec2, b: Vec2) -> Vec2 { return .{a.x + b.x, a.y + b.y}; }
operator - :: (a: Vec2, b: Vec2) -> Vec2 { return .{a.x - b.x, a.y - b.y}; }
operator - :: (a: Vec2) -> Vec2 { return .{-a.x, -a.y}; }
operator * :: (a: Vec2, s: float) -> Vec2 #symmetric { return .{a.x * s, a.y * s}; }
operator == :: (a: Vec2, b: Vec2) -> bool { return a.x == b.x && a.y == b.y; }
operator *= :: (a: *Vec2, s: float) { a.x *= s; a.y *= s; }

print_vec :: (label: string, v: Vec2) { print("%: % %\n", label, v.x, v.y); }

// A fixed-capacity stack with indexing operators.
Stack :: struct {
    items: [8]int;
    count: int;
}
operator [] :: (s: Stack, i: int) -> int { return s.items[i]; }
operator []= :: (s: *Stack, i: int, value: int) { s.items[i] = value; }
push :: (s: *Stack, v: int) { s.items[s.count] = v; s.count += 1; }

// `operator *[]` returns a pointer, so elements can be read and assigned.
Grid :: struct { cells: [4]float; }
operator *[] :: (g: *Grid, i: int) -> *float { return *g.cells[i]; }

main :: () {
    a := Vec2.{1, 2};
    b := Vec2.{3, 5};
    print_vec("a + b", a + b);
    print_vec("b - a", b - a);
    print_vec("-a", -a);
    print_vec("a * 2", a * 2);
    print_vec("3 * a", 3 * a);              // #symmetric
    print_vec("a + b * 2", a + b * 2);      // usual precedence
    print("% % %\n", a == a, a == b, a != b);   // != from ==

    c := a;
    c += b;                                  // no operator +=: c = c + b
    print_vec("c += b", c);
    c *= 10;                                 // operator *=
    print_vec("c *= 10", c);
    c -= .{40, 70};
    print_vec("c -= .{40, 70}", c);

    s: Stack;
    push(*s, 10);
    push(*s, 20);
    s[1] = s[0] + s[1];                      // operator [] and operator []=
    s[0] += 5;                               // s[0] = s[0] + 5 through [] and []=
    print("% % %\n", s[0], s[1], s.count);

    g: Grid;
    g[2] = 1.5;                              // operator *[]
    g[2] += 1;
    print("% %\n", g[2], g.cells[2]);
}
