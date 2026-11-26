// ERROR: #asm: variable 'p' has type P; only integers, enums, floats and pointers can be operands
P :: struct { a: int; }
main :: () { p: P; #asm { inc p } }
