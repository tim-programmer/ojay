// ERROR: '..' needs an array, not s64
f :: (xs: ..int) -> int { return xs.count; }
main :: () { x := 3; f(..x); }
