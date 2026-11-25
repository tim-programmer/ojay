// ERROR: variadic #foreign procedures are not supported yet
printf :: (fmt: *u8, args: ..Any) -> s32 #foreign;
main :: () { printf("x"); }
