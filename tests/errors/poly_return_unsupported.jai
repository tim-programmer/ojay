// ERROR: '$R' in a return type is not supported yet
convert :: (x: $T) -> $R { return cast(R) x; }
main :: () { convert(1); }
