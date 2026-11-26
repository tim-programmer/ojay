#import "Basic";

Transform :: struct { x, y: float; scale := 1.0; }
Info :: struct { name: string; tags: int; }
Header :: struct { using info: *Info; version: int; }

Entity :: struct {
    id: int;
    using transform: *Transform;
    using header: Header;  // a struct that itself uses a pointer
}

move :: (using e: *Entity, dx: float) {
    x += dx;   // e.transform.x
}

main :: () {
    t := Transform.{1, 2, 3};
    info := Info.{"player", 7};
    e: Entity;
    e.id = 1;
    e.transform = *t;
    e.header.info = *info;
    e.version = 2;

    print("% % % %\n", e.x, e.y, e.scale, e.name);
    e.y = 5;
    move(*e, 0.5);
    print("% %\n", t, e.tags);
    e.tags += 1;
    print("%\n", info);

    p := *e;
    p.scale *= 2;
    print("%\n", t.scale);

    using e;
    name = "renamed";
    print("% %\n", info.name, version);

    // At compile time too.
    R :: #run () -> float {
        t := Transform.{x = 4};
        e: Entity;
        e.transform = *t;
        e.x += 1;
        return t.x;
    }();
    print("%\n", R);
}
