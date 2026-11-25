// ERROR: 'volatile_store' works on integers
#import "Hardware";
S :: struct { a: int; }
main :: () { s: S; volatile_store(*s, s); }
