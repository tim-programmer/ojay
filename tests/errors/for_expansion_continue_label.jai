// ERROR: 'continue outer' of a loop with a for_expansion is not supported yet
R :: struct { n: int; }
for_expansion :: (r: *R, body: Code, flags: For_Flags) #expand {
    for i: 0..r.n-1 { `it := i; `it_index := i; #insert body; }
}
main :: () { r := R.{3}; for outer: r { for 1..2 { continue outer; } } }
