// ERROR: a Code_Node of kind .OTHER that compiler_get_nodes did not make
#import "Basic";
#import "Compiler";
bad :: () -> Code #compiler { n := New(Code_Node); return compiler_get_code(n); }
main :: () { x := #insert #run bad(); }
