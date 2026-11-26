// ERROR: member 'marker' of S has type void and cannot be set
S :: struct {
    x: int;
    marker: void;
}
main :: () { s := S.{marker = 1}; }
