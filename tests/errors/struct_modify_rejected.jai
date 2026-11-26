// ERROR: #modify of 'Accumulator' rejected T = string: the element type must be a number
Accumulator :: struct ($T: Type)
#modify {
    info := cast(*Type_Info) T;
    return info.type == .INTEGER || info.type == .FLOAT, "the element type must be a number";
}
{ total: T; }
main :: () { a: Accumulator(string); }
