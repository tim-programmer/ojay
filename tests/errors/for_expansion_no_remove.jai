// ERROR: the for_expansion of this loop does not support 'remove'
// ERROR: #insert (remove = { ... }) body;
R :: struct { n: int; }
for_expansion :: (r: *R, body: Code, flags: For_Flags) #expand {
    for i: 0..r.n-1 { `it := i; `it_index := i; #insert body; }
}
main :: () { r := R.{3}; for r if it == 1 remove it; }
