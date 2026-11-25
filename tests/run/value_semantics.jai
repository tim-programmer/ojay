#import "Basic";

// Structs and fixed arrays are values: assignment and parameters copy them.
// Views and pointers refer to the original storage.

Point :: struct { x, y: int; }
Polygon :: struct {
    points: [3]Point;
    name: string;
}

bump_copy :: (arr: [4]int) -> int {
    arr[0] = 999;            // modifies the callee's copy only
    return arr[0] + arr[3];
}

bump_view :: (arr: []int) { arr[0] = 999; }

move :: (p: Point, dx: int) -> Point {
    p.x += dx;
    return p;
}

main :: () {
    a := Point.{1, 2};
    b := a;
    b.x = 10;
    print("a=% b=%\n", a, b);

    nums := int.[1, 2, 3, 4];
    copy := nums;
    copy[1] = 20;
    print("nums=% copy=%\n", nums, copy);
    print("bump_copy returned %, nums=%\n", bump_copy(nums), nums);
    bump_view(nums);
    print("after bump_view: %\n", nums);

    print("move: % (original %)\n", move(a, 5), a);

    poly: Polygon;
    poly.name = "tri";
    poly.points[1] = .{3, 4};
    other := poly;
    other.points[1].y = 40;
    print("poly=% other=%\n", poly.points[1], other.points[1]);

    p := *poly.points[2];
    p.x = 7;
    print("through pointer: %\n", poly.points[2]);

    // Swapping struct values
    c, d := Point.{1, 1}, Point.{2, 2};
    c, d = d, c;
    print("swapped: % %\n", c, d);

    // Comparing fields of copies
    e := poly;
    print("same name: %, same x: %\n", e.name == poly.name, e.points[2].x == poly.points[2].x);
}
