// ERROR: a #c_call procedure written in Jai cannot be variadic (only #foreign ones can: C varargs)
f :: (fmt: *u8, args: ..) #c_call {}
main :: () {}
