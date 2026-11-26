// ERROR: #modify of 'Table' rejected Key_Type = P, Value_Type = s64, LOAD_FACTOR_PERCENT = 70: keys must be integers, enums, bools, floats, pointers or strings
#import "Hash_Table";
P :: struct { x: int; }
main :: () { t: Table(P, int); }
