#import "Basic";

Vec2 :: struct { x, y: float; }

Entity :: struct {
    name: string;
    using position: Vec2;   // position.x is also entity.x
    health := 100;
}

Player :: struct {
    using entity: Entity;   // nested: player.x reaches player.entity.position.x
    score: int;
}

Color :: enum { RED; GREEN; BLUE; }

move :: (using e: *Entity, dx: float, dy: float) {
    x += dx;            // e.x
    y += dy;
    health -= 1;
}

length_squared :: (using v: Vec2) -> float { return x*x + y*y; }

main :: () {
    p: Player;
    p.name = "hero";
    p.x = 1;
    p.entity.position.y = 2;
    print("% % % %\n", p.name, p.x, p.y, p.health);

    move(*p.entity, 2, 3);
    print("% % %\n", p.x, p.y, p.health);
    print("%\n", length_squared(p.position));

    {
        using p;
        score = 42;
        name = "renamed";
        print("% % %\n", name, score, x);
    }
    print("% %\n", p.score, p.name);

    using other := Entity.{name = "orc"};
    print("% %\n", name, health);   // other.name, other.health

    using Color;
    c := GREEN;
    print("% %\n", c, cast(int) BLUE);

    // A local declaration shadows a member brought in by 'using'.
    health := 7;
    print("% %\n", health, other.health);
}
