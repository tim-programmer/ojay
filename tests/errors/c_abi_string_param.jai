// ERROR: #c_call/#foreign parameters of type string are not supported: string is not a C type
puts :: (s: string) -> s32 #foreign;
main :: () { puts("hi"); }
