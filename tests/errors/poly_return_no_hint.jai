// ERROR: cannot infer '$R' of 'convert': it is introduced in the return type, and the call's result is not used where a type is expected
#import "Basic";
convert :: (x: $T) -> $R { return cast(R) x; }
main :: () { print("%\n", convert(1)); }
