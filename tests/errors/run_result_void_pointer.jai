// ERROR: points to data of unknown type outside the heap
#import "Basic";
global_value: int = 5;
P :: #run () -> *void { return *global_value; }();
main :: () { p := P; }
