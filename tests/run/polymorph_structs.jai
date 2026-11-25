// Polymorphic structs: struct ($T: Type, $N: int), instances in types and
// literals, cached instances, polymorphic procedures and operators over them.
#import "Basic";
#import "Containers";
C :: #import "Containers";

Vector :: struct ($T: Type, $N: int) {
    data: [N]T;
}

operator + :: (a: Vector($T, $N), b: Vector(T, N)) -> Vector(T, N) {
    r: Vector(T, N);
    for 0..N-1 r.data[it] = a.data[it] + b.data[it];
    return r;
}

operator * :: (v: Vector($T, $N), k: T) -> Vector(T, N) #symmetric {
    r: Vector(T, N);
    for 0..N-1 r.data[it] = v.data[it] * k;
    return r;
}

operator == :: (a: Vector($T, $N), b: Vector(T, N)) -> bool {
    for 0..N-1 if a.data[it] != b.data[it] return false;
    return true;
}

dot :: (a: Vector($T, $N), b: Vector(T, N)) -> T {
    s: T;
    for 0..N-1 s += a.data[it] * b.data[it];
    return s;
}

dimensions :: (v: Vector($T, $N)) -> int { return N; }

// A parameter with a default, and one whose type depends on an earlier one.
Buffer :: struct ($T: Type, $CAPACITY := 4, $FILL: T) {
    items: [CAPACITY]T = .[FILL, FILL, FILL, FILL];
    count: int;
}

// A struct that refers to its own instance.
Node :: struct ($T: Type) {
    value: T;
    next: *Node(T);
}

list_length :: (head: *Node($T)) -> int {
    n := 0;
    while head {
        n += 1;
        head = head.next;
    }
    return n;
}

Vec3 :: Vector(float, 3);   // a named instance

main :: () {
    a := Vector(float, 3).{.[1, 2, 3]};
    b: Vec3 = .{.[0.5, 0.5, 0.5]};
    c := a + b;
    print("vector: % %\n", c, type_of(c));
    print("scaled: % %\n", a * 2, 3 * a);
    print("dot: % dims: % %\n", dot(a, b), dimensions(a), dimensions(Vector(int, 5).{}));

    // Two instances of one template are distinct, independent types.
    i := Vector(int, 2).{.[3, 4]};
    j := Vector(int, 2).{.[10, 20]};
    print("ints: % % %\n", i + j, dot(i, j), type_of(i));
    print("sizes: % % %\n", size_of(Vector(u8, 3)), size_of(Vector(float64, 3)), size_of(Vec3));
    print("equal: % % %\n", i == Vector(int, 2).{.[3, 4]}, i == j, i + j == j + i);

    // Instances are cached by their parameters: equal arguments, same type.
    print("same type: % % %\n", Vec3 == Vector(float32, 1 + 2), Vector(int, 2) == Vector(s64, 2),
          Vector(int, 2) == Vector(int, 3));
    print("names: % %\n", Vector(Vector(u8, 2), 1), Buffer(string, FILL = "-"));

    buf: Buffer(int, FILL = 7);
    print("buffer: % % %\n", buf.items, buf.count, type_of(buf));
    big: Buffer(bool, 4, true);
    print("buffer: %\n", big.items);

    n3 := Node(string).{"c", null};
    n2 := Node(string).{"b", *n3};
    n1 := Node(string).{"a", *n2};
    print("list: % % %\n", list_length(*n1), n1.next.next.value, type_of(n1.next));

    // Polymorphic structs from a module, also by qualified name.
    s: Stack(float);
    push(*s, 1.5);
    push(*s, 2.5);
    q: C.Stack(float);
    print("module: % % %\n", pop(*s), type_of(s) == type_of(q), type_of(C.make_pair(1, true)));
}
