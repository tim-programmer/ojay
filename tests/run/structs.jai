#import "Basic";

Vec3 :: struct {
    x, y, z: float;
}

Entity :: struct {
    name: string;
    pos: Vec3;
    health: int = 100;
    alive := true;
}

Node :: struct {
    value: int;
    next: *Node;
}

add :: (a: Vec3, b: Vec3) -> Vec3 {
    return .{a.x + b.x, a.y + b.y, a.z + b.z};
}

damage :: (e: *Entity, amount: int) {
    e.health -= amount;
    if e.health <= 0 then e.alive = false;
}

main :: () {
    e: Entity;
    e.name = "orc";
    print("% % % %\n", e.name, e.health, e.alive, e.pos);
    damage(*e, 30);
    print("after: %\n", e.health);
    damage(*e, 80);
    print("dead: % %\n", e.health, e.alive);

    v := add(Vec3.{1, 2, 3}, .{x = 10, z = 30});
    print("v = %\n", v);
    print("v.y = %\n", add(v, v).y);

    p := *e;
    p.pos.x = 5;
    print("p.pos = %, size_of(Vec3) = %\n", p.pos, size_of(Vec3));

    // linked list
    n3 := Node.{3, null};
    n2 := Node.{2, *n3};
    n1 := Node.{1, *n2};
    node := *n1;
    total := 0;
    while node {
        total += node.value;
        node = node.next;
    }
    print("list total = %\n", total);

    e2 := Entity.{name = "elf"};
    print("% % %\n", e2.name, e2.health, e2.alive);
}
