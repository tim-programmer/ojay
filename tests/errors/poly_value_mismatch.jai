// ERROR: 'identity' with T = s64 has type (s64) -> s64, not (s64) -> float32
identity :: (x: $T) -> T { return x; }
main :: () { f: (int) -> float = identity; }
