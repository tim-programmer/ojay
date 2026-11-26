// ERROR: is part of a cycle of pointers, which cannot be baked
#import "Basic";
Node :: struct { next: *Node; }
LOOP :: #run () -> *Node { n := New(Node); n.next = n; return n; }();
main :: () { x := LOOP; }
