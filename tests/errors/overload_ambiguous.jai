// ERROR: ambiguous call to overloaded procedure 'f'
f :: (a: int, b: float) {}
f :: (a: float, b: int) {}
main :: () { f(1, 1); }
