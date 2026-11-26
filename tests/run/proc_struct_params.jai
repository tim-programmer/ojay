#import "Basic";

Sorter :: struct ($T: Type, $less: (T, T) -> bool) { items: [..] T; }

insert :: (s: *Sorter($T, $L), x: T) {
    array_add(*s.items, x);
    i := s.items.count - 1;
    while i > 0 && s.less(s.items[i], s.items[i - 1]) {
        swap(*s.items[i], *s.items[i - 1]);
        i -= 1;
    }
}

ascending :: (a: int, b: int) -> bool { return a < b; }

// A compile-time procedure parameter of a procedure.
apply_twice :: ($f: (int) -> int, x: int) -> int { return f(f(x)); }
inc :: (x: int) -> int { return x + 1; }

main :: () {
    up: Sorter(int, ascending);
    for int.[5, 1, 4, 2]  insert(*up, it);
    print("%\n", up.items);

    down: Sorter(int, (a: int, b: int) -> bool { return a > b; });
    for int.[5, 1, 4, 2]  insert(*down, it);
    print("%\n", down.items);

    by_length: Sorter(string, (a: string, b: string) -> bool { return a.count < b.count; });
    for string.["ccc", "a", "bb"]  insert(*by_length, it);
    print("%\n", by_length.items);

    print("% %\n", apply_twice(inc, 5), type_of(up));
    print("%\n", Sorter(int, ascending) == type_of(up));

    N :: #run () -> int {
        s: Sorter(int, ascending);
        for int.[3, 1, 2]  insert(*s, it);
        return s.items[0] * 100 + s.items[2];
    }();
    print("%\n", N);
}
