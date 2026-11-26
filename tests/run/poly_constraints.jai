#import "Basic";

Entity :: struct { id: int; name: string; }
Player :: struct { using base: Entity; score: int; }
Rock :: struct { weight: float; }

describe :: (e: *$T/Entity) { print("% #% (%)\n", e.name, e.id, T); }

Has_Position :: struct { x: float; y: float; }
Point :: struct { x: float; y: float; }
Sprite :: struct { image: string; x: float; y: float; layer: int; }

move :: (p: *$T/interface Has_Position, dx: float) {
    p.x += dx;
    p.y += dx;
}

Vec :: struct ($N: int) { v: [N] float; }
total :: (a: $T/Vec) -> float {
    s: float;
    for a.v  s += it;
    return s;
}

// Overloads: the constraint picks one.
kind :: (x: $T/Entity) -> string { return "entity"; }
kind :: (x: $T/interface Has_Position) -> string { return "positioned"; }

main :: () {
    e := Entity.{1, "crate"};
    p := Player.{base = .{2, "hero"}, score = 10};
    describe(*e);
    describe(*p);

    pt := Point.{1, 2};
    sp := Sprite.{"tree.png", 5, 6, 1};
    move(*pt, 1);
    move(*sp, 0.5);
    print("% %\n", pt, sp);

    a: Vec(3);
    a.v = .[1, 2, 3.5];
    b: Vec(2);
    b.v = .[10, 20];
    print("% %\n", total(a), total(b));

    print("% % %\n", kind(e), kind(p), kind(sp));
}
