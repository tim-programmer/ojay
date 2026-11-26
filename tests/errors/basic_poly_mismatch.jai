// ERROR: but 'array_find' with T = s64 expects s64
#import "Basic";
main :: () { a := int.[1, 2]; array_find(a, "x"); }
