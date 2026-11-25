// ERROR: cannot use 'context' in #no_context procedure 'raw'
raw :: () -> u32 #no_context {
    return context.user_index;
}
main :: () { raw(); }
