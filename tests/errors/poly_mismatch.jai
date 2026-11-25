// ERROR: argument for 'b' has type string, but 'same' with T = s64 expects s64
#import "Basic";
same :: (a: $T, b: T) -> bool { return a == b; }
main :: () { x := 1; print("%\n", same(x, "one")); }
