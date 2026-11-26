#import "Basic";

// Only numeric element types; integers are stored as s64.
Accumulator :: struct ($T: Type)
#modify {
    info := cast(*Type_Info) T;
    if info.type == .INTEGER  { T = s64; return true; }
    if info.type == .FLOAT    return true;
    return false, "the element type must be a number";
}
{
    total: T;
    count: int;
}

add :: (a: *Accumulator($T), x: T) { a.total += x; a.count += 1; }

Fixed :: struct ($N: int) #modify { return N > 0 && N <= 4, "N must be 1..4"; } {
    values: [N] u8;
}

main :: () {
    a: Accumulator(u8);
    add(*a, 200);
    add(*a, 200);
    print("% % %\n", a.total, type_of(a.total), type_of(a));
    print("%\n", Accumulator(u8) == Accumulator(s64));

    f: Accumulator(float64);
    add(*f, 1.5);
    print("% %\n", f.total, type_of(f));

    k: Fixed(3);
    print("%\n", k);
}
