// ERROR: #bake_constants: 'b' is not a compile-time parameter ($b); use #bake_arguments
add :: (a: int, b: int) -> int { return a + b; }
bad :: #bake_constants add(b = 1);
main :: () { bad(1); }
