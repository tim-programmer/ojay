// ERROR: <Basic>:
// ERROR: P and P
#import "Basic";
P :: struct { x: int; }
main :: () { a: [..]P; array_find(a, P.{1}); }
