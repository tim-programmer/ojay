// ERROR: #place zz: 'zz' is not an earlier member of struct S
S :: struct {
    a: int;
    #place zz;
    b: float64;
}
main :: () { s: S; }
