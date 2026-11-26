// ERROR: 'array_reserve' needs a pointer to a dynamic array ([..]T)
#import "Basic";
main :: () { a: [4]int; array_reserve(*a, 10); }
