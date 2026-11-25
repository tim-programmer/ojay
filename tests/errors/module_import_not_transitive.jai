// ERROR: undeclared identifier 'floor' (did you forget #import "Math"?)
#import "Geometry";   // Geometry imports Math, but that does not make Math visible here
main :: () { x := floor(1.5); }
