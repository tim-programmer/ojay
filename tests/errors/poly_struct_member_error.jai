// ERROR: in Vector(s64, -1), used at
Vector :: struct ($T: Type, $N: int) { data: [N]T; }
main :: () { v: Vector(int, -1); }
