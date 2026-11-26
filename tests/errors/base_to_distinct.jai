// ERROR: type mismatch: expected Handle, got u32
Handle :: #type,distinct u32;
take :: (h: Handle) {}
main :: () {
    u: u32 = 1;
    take(u);
}
