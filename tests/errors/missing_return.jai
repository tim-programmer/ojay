// ERROR: does not return a value on all paths
f :: (x: int) -> int { if x > 0 return 1; }
main :: () { f(1); }
