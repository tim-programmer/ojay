// ERROR: cannot iterate over a value of type P (it is not an array, and no for_expansion is declared for it)
P :: struct { x: int; }
main :: () { p: P; for p {} }
