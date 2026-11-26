// ERROR: past the end of the Type_Info_Integer of s64
// ERROR: a Type_Info cast to the wrong Type_Info_* struct?
#import "Basic";
f :: () -> int { s := cast(*Type_Info_Struct) int; return s.members.count; }
X :: #run f();
