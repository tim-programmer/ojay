// ERROR: #modify is only allowed on polymorphic procedures
f :: (x: int) #modify { return true; } { }
main :: () { f(1); }
