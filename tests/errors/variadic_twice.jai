// ERROR: a procedure can have only one variadic parameter
f :: (xs: ..int, ys: ..int) {}
main :: () { f(1); }
