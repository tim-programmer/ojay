// ERROR: not an lvalue
f :: () -> int { return 1; }
main :: () { f() = 2; }
