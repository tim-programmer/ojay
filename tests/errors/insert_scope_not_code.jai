// ERROR: #insert,scope(...) needs a Code constant
main :: () { x := 1; #insert,scope(x) "y := 2;"; }
