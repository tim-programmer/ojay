// ERROR: push_context needs a value of type Context, not *Context
main :: () {
    c := context;
    push_context *c {
    }
}
