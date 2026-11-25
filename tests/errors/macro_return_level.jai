// ERROR: procedure returns 0 values, but 1 was given
// ERROR: in the expansion of macro 'leave' at
// ERROR: macro_return_level.jai:7:5
// `return leaves the caller, here a macro that returns nothing.
leave :: (x: int) #expand { if x > 0 `return x; }
outer :: (x: int) #expand {
    leave(x);
}
f :: () -> int { outer(1); return 0; }
main :: () { f(); }
