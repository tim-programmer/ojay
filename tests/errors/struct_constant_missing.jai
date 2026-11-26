// ERROR: type S has no constant member 'M'
S :: struct {
    N :: 3;
}
main :: () { x := S.M; }
