// ERROR: type mismatch: expected Index, got u32
Index :: #type,isa u32;
main :: () {
    u: u32 = 1;
    i: Index = u;
}
