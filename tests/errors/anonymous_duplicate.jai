// ERROR: duplicate member 'value'
S :: struct {
    value: u32;
    union {
        value: s32;
        other: float;
    }
}
main :: () { s: S; }
