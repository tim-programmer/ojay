// ERROR: cannot pass string as a C variadic argument (only integers, floats, bools, enums and pointers; for a string pass to_c_string(s) or s.data)
printf :: (fmt: *u8, args: ..) -> s32 #foreign;
main :: () { printf("%s".data, "hello"); }
