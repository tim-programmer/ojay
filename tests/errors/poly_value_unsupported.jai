// ERROR: 'identity' is a polymorphic procedure; using it as a value without calling it
identity :: (x: $T) -> T { return x; }
main :: () { f := identity; }
