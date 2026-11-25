// ERROR: cannot use 'context' in #c_call procedure 'callback': it has no context
callback :: (x: s32) -> s32 #c_call {
    a := context.allocator;
    return x;
}
main :: () { f: (s32) -> s32 #c_call = callback; f(1); }
