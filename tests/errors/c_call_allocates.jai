// ERROR: 'New' needs a context, but #c_call procedure 'isr' has none
#import "Basic";
isr :: () #c_call {
    p := New(int);
}
main :: () { isr(); }
