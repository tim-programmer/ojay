// ERROR: polymorphic procedure 'times' has compile-time parameter '$N', so it cannot be used as a value
times :: (x: int, $N: int) -> int { return x * N; }
main :: () { f: (int, int) -> int = times; }
