// ERROR: it is not an instance of Vec
Vec :: struct ($N: int) { v: [N] float; }
total :: (a: $T/Vec) {}
main :: () { total(3); }
