// ERROR: an array passed with '..' must be the only argument of the variadic parameter
f :: (xs: ..int) -> int { return xs.count; }
main :: () { a := int.[1, 2]; f(0, ..a); }
