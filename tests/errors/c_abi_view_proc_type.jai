// ERROR: #c_call/#foreign parameters of type []s32 are not supported: array views and dynamic arrays are not C types
Callback :: #type (values: []s32) #c_call;
main :: () { cb: Callback; }
