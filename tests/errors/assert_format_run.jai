// ERROR: assertion failed: y is 3
#import "Basic";
X :: #run () -> int { y := 3; assert(y == 4, "y is %", y); return 0; }();
main :: () {}
