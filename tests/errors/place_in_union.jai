// ERROR: #place is not allowed in a union
U :: union {
    a: int;
    #place a;
    b: float64;
}
main :: () { u: U; }
