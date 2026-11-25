// Anonymous struct and union members: their members are accessed as if they
// belonged to the enclosing struct.
#import "Basic";

Value :: struct {
    tag: u8;
    union {
        i: s32;
        f: float32;
        struct { lo: u16; hi: u16; }
    }
    after: u32;
}

Register_Block :: struct {
    CTRL: u32;
    union {
        MODE_OUTPUT: u32;
        MODE_INPUT: u32;
    }
    CHANNEL: [2] struct { VALUE: u32; _reserved: [12]u8; };
}

high :: (v: Value) -> u16 { return v.hi; }   // by value: extracted from the aggregate

main :: () {
    v: Value;
    v.i = 0x12345678;
    print("% % % %\n", size_of(Value), v.lo == 0x5678, v.hi == 0x1234, high(v) == 0x1234);
    p := *v;
    p.lo = 1;
    p.after = 7;
    print("% %\n", v.i == 0x12340001, v.after);
    v.f = 1.5;
    print("%\n", v.f);

    r: Register_Block;
    r.MODE_OUTPUT = 5;
    r.CHANNEL[1].VALUE = 9;
    print("% % %\n", size_of(Register_Block), r.MODE_INPUT, r.CHANNEL[1].VALUE);
    print("%\n", cast(int) *r.CHANNEL[1].VALUE - cast(int) *r);
}
