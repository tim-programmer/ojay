#import "Basic";

// What print shows for various types.

Kind :: enum { CIRCLE; SQUARE; }

Shape :: struct {
    kind: Kind;
    name: string;
    size: float;
    tags: [3]u8;
    visible: bool;
    parent: *Shape;
}

Pair :: struct { a: s16; b: [2]string; }

main :: () {
    s := Shape.{.SQUARE, "box", 2.5, .[1, 2, 3], true, null};
    print("%\n", s);
    shapes := Shape.[s, .{name = "dot", size = 0.125}];
    print("%\n", shapes);
    print("nested: %\n", Pair.{-7, .["x", "yz"]});

    a: Any = 42;
    print("any int: %, type %\n", a, a.type);
    a = "text";
    print("any string: %, type %\n", a, a.type);
    a = s.kind;
    print("any enum: %, type %\n", a, a.type);

    print("types: % % % % %\n", type_of(s), type_of(shapes), type_of(*s), type_of(s.tags), int);
    empty: []int;
    print("empty view: %, count %\n", empty, empty.count);
    ptr: *int;
    print("null pointer: %\n", ptr);
    print("chars: % %\n", #char "x", cast(u8) #char "x");
    print("% and %% and %\n", 1, 2);
    print("to stderr\n", to_standard_error = true);
    n := print("counted\n");
    print("print returned %\n", n);
    write_string("write_string works\n");
}
