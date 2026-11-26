// ERROR: type mismatch: expected Other, got Index
Index :: #type,isa u32;
Other :: #type,distinct u32;
main :: () {
    i: Index = 1;
    o: Other = i;
}
