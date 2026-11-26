// ERROR: it follows the variadic parameter, so it can only be passed by name
f :: (xs: ..int, k := 0) {}
main :: () { p := f; p(1, 2); }
