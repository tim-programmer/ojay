// ERROR: keys must be integers, enums, bools, floats, pointers or strings (or give a given_hash_function and a given_compare_function)
#import "Hash_Table";
P :: struct { x: int; }
main :: () { t: Table(P, int); }
