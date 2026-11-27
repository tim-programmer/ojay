// ERROR: did you forget #import "Compiler"
show :: (c: Code) #expand { root, exprs := compiler_get_nodes(c); }
main :: () { show(#code 1 + 2); }
