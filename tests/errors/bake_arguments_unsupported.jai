// ERROR: #bake_arguments is not supported yet
add :: (a: int, b: int) -> int { return a + b; }
add_one :: #bake_arguments add(b = 1);
