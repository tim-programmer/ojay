// ERROR: members 'a' and 'b' of S overlap (#place) and both have default values
S :: struct {
    a: s64 = 1;
    #place a;
    b: float64 = 2;
}
main :: () { s: S; }
