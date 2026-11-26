// ERROR: 'identity' is a polymorphic procedure; to use it as a value, use it where a procedure type is expected
identity :: (x: $T) -> T { return x; }
main :: () { f := identity; }
