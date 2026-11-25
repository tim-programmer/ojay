// ERROR: contains itself by value
S :: struct { s: S; }
main :: () { a: S; }
