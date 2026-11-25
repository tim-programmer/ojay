// ERROR: #c_call/#foreign procedures cannot return multiple values
S :: struct { a: s32; }
two :: (x: S) -> S, s32 #foreign;
main :: () { a, b := two(.{1}); }
