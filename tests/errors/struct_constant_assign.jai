// ERROR: cannot assign to constant 'N'
S :: struct {
    N :: 3;
    x: int;
}
main :: () {
    S.N = 4;
}
