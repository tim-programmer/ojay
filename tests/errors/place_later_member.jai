// ERROR: #place b: 'b' is not an earlier member of struct S
S :: struct {
    a: int;
    #place b;
    b: float64;
}
main :: () { s: S; }
