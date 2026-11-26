// ERROR: '$R' can only be introduced in the first return type
f :: (x: $T) -> T, $R { r: R; return x, r; }
main :: () { a, b := f(1); }
