// ERROR: #c_call/#foreign procedures cannot return multiple values
main :: () { f: (s32) -> (s32, s32) #c_call; }
