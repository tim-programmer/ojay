// ERROR: Code_Node introspection (compiler_get_nodes) is not supported yet
show :: (c: Code) #expand {
    root, exprs := compiler_get_nodes(c);
}
main :: () { show(#code 1 + 2); }
