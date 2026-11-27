// ERROR: 'compiler_get_nodes' is a #compiler procedure
#import "Compiler";
main :: () { root, all := compiler_get_nodes(#code 1 + 2); }
