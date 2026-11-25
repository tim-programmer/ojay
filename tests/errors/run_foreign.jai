// ERROR: cannot call #foreign procedure 'abs' at compile time
libc :: #system_library "libc";
abs :: (x: s32) -> s32 #foreign libc;
X :: #run abs(-3);
