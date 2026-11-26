// ERROR: type mismatch: Handle + u32
Handle :: #type,distinct u32;
main :: () {
    h: Handle = 1;
    u: u32 = 2;
    x := h + u;
}
