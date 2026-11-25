// Polymorphic code in a module. `grow` is only used by the module itself; the
// instances that main creates are checked in this module's scope, so they see
// it (and Basic, which main need not import for them).
#import "Basic";

Stack :: struct ($T: Type) {
    items: [..]T;
}

push :: (s: *Stack($T), value: T) {
    grow(*s.items);
    s.items[s.items.count - 1] = value;
}

pop :: (s: *Stack($T)) -> T {
    assert(s.items.count > 0, "pop from an empty stack");
    value := s.items[s.items.count - 1];
    s.items.count -= 1;
    return value;
}

// Makes room for one more element at the end.
grow :: (a: *[..]$T) {
    if a.count == a.allocated {
        new_allocated := ifx a.allocated then a.allocated * 2 else 4;
        a.data = realloc(a.data, new_allocated * size_of(T), a.allocated * size_of(T));
        a.allocated = new_allocated;
    }
    a.count += 1;
}

Pair :: struct ($A: Type, $B: Type) { first: A; second: B; }

make_pair :: (a: $A, b: $B) -> Pair(A, B) { return .{a, b}; }
