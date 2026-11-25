// ERROR: 'using' on a pointer member is not supported yet
Base :: struct { a: int; }
Derived :: struct { using base: *Base; }
main :: () { d: Derived; }
