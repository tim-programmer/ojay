// ERROR: a union member cannot have type void
U :: union {
    a: int;
    marker: void;
}
main :: () { u: U; }
