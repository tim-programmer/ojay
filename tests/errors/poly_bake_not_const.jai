// ERROR: parameter 'N' of 'repeat' is a compile-time parameter; its argument must be a constant
repeat :: ($N: int, v: int) -> [N]int { r: [N]int; return r; }
main :: () { n := 3; repeat(n, 1); }
