// ERROR: cannot infer '$T' of 'clear'
clear :: (p: *$T) { p.* = .{}; }
main :: () { clear(null); }
