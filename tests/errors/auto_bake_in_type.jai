// ERROR: '$$' is only allowed before a parameter name ($$x: int)
f :: (x: $$T) {}
main :: () { f(1); }
