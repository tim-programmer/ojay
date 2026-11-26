// ERROR: a void member cannot have a value
S :: struct {
    marker: void = 1;
}
main :: () { s: S; }
