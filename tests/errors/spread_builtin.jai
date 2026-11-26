// ERROR: 'memset' has no variadic parameter to pass '..' to
#import "Basic";
main :: () { a := int.[1, 2]; memset(null, 0, ..a); }
