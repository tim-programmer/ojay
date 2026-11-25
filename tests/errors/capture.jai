// ERROR: cannot use local variable
main :: () { x := 1; f :: () -> int { return x; } }
