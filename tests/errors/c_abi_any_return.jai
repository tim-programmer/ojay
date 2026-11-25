// ERROR: #c_call/#foreign return type Any is not supported: Any is not a C type
get :: () -> Any #c_call { return 1; }
main :: () { get(); }
