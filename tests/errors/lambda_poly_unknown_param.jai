// ERROR: cannot infer the parameter types of this lambda for 'f' of 'bad'
bad :: (f: ($A) -> int) {}
main :: () { bad((x) => 1); }
