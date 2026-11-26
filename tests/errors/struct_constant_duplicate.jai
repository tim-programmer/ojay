// ERROR: duplicate member 'x'
S :: struct {
    x :: 3;
    x: int;
}
main :: () { s: S; }
