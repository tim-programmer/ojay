// ERROR: #modify is only allowed on polymorphic structs
S :: struct #modify { return true; } { x: int; }
main :: () {}
