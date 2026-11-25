// ERROR: polymorphic struct 'Vector' takes 2 parameters (T, N), but 1 was given
Vector :: struct ($T: Type, $N: int) { data: [N]T; }
main :: () { v: Vector(int); }
