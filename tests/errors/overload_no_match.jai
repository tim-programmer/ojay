// ERROR: no overload of 'f' accepts (string)
f :: (a: int) {}
f :: (a: float) {}
main :: () { f("text"); }
