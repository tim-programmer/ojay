// ERROR: 'Vector' is a polymorphic struct; give its parameters
Vector :: struct ($T: Type) { x: T; }
main :: () { v: Vector; }
