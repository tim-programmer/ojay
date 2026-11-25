// ERROR: no overload of 'pick' accepts (string)
// ERROR: #modify of 'pick' rejected T = string: numbers only
pick :: (x: $T) #modify { return T != string, "numbers only"; } { }
pick :: (x: bool) { }
main :: () { pick("text"); }
