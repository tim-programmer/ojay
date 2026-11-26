// ERROR: cannot use local variable 'k' of an enclosing procedure
main :: () { k := 3; h := (x: int) => x + k; }
