// ERROR: type mismatch: expected u32, got Handle
Handle :: #type,distinct u32;
main :: () {
    h: Handle = 1;
    u: u32 = h;
}
