// ERROR: calling 'helper' needs a context, but #c_call procedure 'handler' has none (give it one with push_context)
helper :: () -> int { return 1; }
handler :: () #c_call {
    helper();
}
main :: () { handler(); }
