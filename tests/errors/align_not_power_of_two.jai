// ERROR: #align 12: the alignment must be a power of two
S :: struct {
    x: u8 #align 12;
}
main :: () { s: S; }
