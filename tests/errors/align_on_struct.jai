// ERROR: #align cannot be applied to a struct
S :: struct {
    x: u8;
} #align 64
main :: () { s: S; }
