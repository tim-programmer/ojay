// ERROR: #bake_arguments must be the value of a constant
add :: (a: int, b: int) -> int { return a + b; }
main :: () { f := #bake_arguments add(b = 1); }
