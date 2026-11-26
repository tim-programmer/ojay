#import "Basic";
#import "Hash_Table";

Point :: struct { x, y: int; }
hash_point :: (p: Point) -> u32 { return cast,trunc(u32) (p.x * 31 + p.y); }
points_equal :: (a: Point, b: Point) -> bool { return a.x == b.x && a.y == b.y; }

// A bare polymorphic struct name: any instance.
describe :: (t: *Table) -> string { return tprint("% entries of %", t.count, type_of(t.*)); }
// Fewer arguments than parameters: the others can be anything.
total :: (t: *Table($K, int)) -> int {
    s := 0;
    for t.*  s += it;
    return s;
}

Pair :: struct ($A: Type, $B: Type = int) { a: A; b: B; }
show_first :: (p: Pair) { print("% (%)\n", p.a, p.A); }

main :: () {
    plain: Table(string, int);
    table_add(*plain, "x", 1);
    table_add(*plain, "y", 2);
    custom: Table(Point, int, hash_point, points_equal);
    table_add(*custom, .{1, 2}, 40);
    print("%\n", describe(*plain));
    print("%\n", describe(*custom));
    print("% %\n", total(*plain), total(*custom));
    p: Pair(string, float);
    p.a = "first";
    show_first(p);
}
