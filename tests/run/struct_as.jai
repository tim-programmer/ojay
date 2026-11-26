#import "Basic";

Entity :: struct { id: int; name: string; }
Player :: struct {
    #as using base: Entity;
    score: int;
}
Boss :: struct {
    health := 100;
    #as using player: Player;   // a Boss is a Player, and so an Entity
}

Meters :: struct { #as value: float; }

describe :: (e: Entity) { print("entity % %\n", e.id, e.name); }
rename :: (e: *Entity, n: string) { e.name = n; }
twice :: (x: float) -> float { return x * 2; }

make_player :: () -> Player { return .{base = .{7, "made"}, score = 1}; }

main :: () {
    p: Player;
    p.id = 1;
    p.name = "hero";
    p.score = 50;
    describe(p);
    rename(*p, "renamed");
    print("%\n", p);

    b: Boss;
    b.id = 2;
    b.name = "dragon";
    describe(b);
    rename(*b, "big dragon");
    e: Entity = b;
    print("% %\n", e, b.health);

    describe(make_player());

    m := Meters.{2.5};
    print("%\n", twice(m));

    list: [..] *Entity;
    array_add(*list, *p);
    array_add(*list, *b);
    for list  print("% ", it.name);
    print("\n");
}
