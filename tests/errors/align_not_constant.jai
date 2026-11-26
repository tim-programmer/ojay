// ERROR: #align needs a compile-time integer constant
n := 16;
S :: struct {
    x: u8 #align n;
}
main :: () { s: S; }
