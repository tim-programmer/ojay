// ERROR: #bake_arguments: 'add' has no parameter or type variable named 'c'
add :: (a: int, b: int) -> int { return a + b; }
bad :: #bake_arguments add(c = 1);
main :: () { bad(1, 2); }
