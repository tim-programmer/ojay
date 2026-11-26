// ERROR: cannot assign to constant 'N'
S :: struct {
    N :: 3;
    x: int;
}
main :: () {
    s: S;
    s.N = 4;
}
