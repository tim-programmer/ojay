// ERROR: an array passed with '..' must be the only argument of the variadic parameter
f :: (xs: ..int, k := 0) {}
main :: () { a := int.[1]; f(..a, 2); }
