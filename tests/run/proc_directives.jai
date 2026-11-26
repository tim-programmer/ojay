// WARNING: 'old_api' is deprecated: use new_api
// WARNING: 'older' is deprecated
#import "Basic";

old_api :: (x: int) -> int #deprecated "use new_api" { return x + 1; }
older :: () #deprecated {}

checked :: (x: int) -> int #must { return x * 2; }

// No bounds checks: reading past the end is not caught (the slot holds 99).
peek_past :: (a: [] int, i: int) -> int #no_abc { return a[i]; }

square :: (x: int) -> int { return x * x; }
cube :: inline (x: int) -> int { return x * x * x; }

main :: () {
    print("%\n", old_api(1));
    older();
    y := checked(4);
    print("%\n", y);
    print("%\n", inline square(7));
    print("%\n", (inline square(3)) + 1);
    print("%\n", cube(2));

    storage := int.[1, 2, 3, 99];
    view: [] int;
    view.data = storage.data;
    view.count = 3;
    print("%\n", peek_past(view, 3));
}
