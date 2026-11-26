// ERROR: #bake_arguments: the value of 'b' must be a compile-time constant
add :: (a: int, b: int) -> int { return a + b; }
x := 3;
add_x :: #bake_arguments add(b = x);
main :: () { add_x(1); }
