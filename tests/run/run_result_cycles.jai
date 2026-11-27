// #run results with cycles of pointers, shared objects and *void pointers.
#import "Basic";
Node :: struct { value: int; next: *Node; other: *void; }
make_ring :: () -> *Node {
    a := New(Node); b := New(Node); c := New(Node);
    a.value = 1; b.value = 2; c.value = 3;
    a.next = b; b.next = c; c.next = a;
    bytes := cast(*u8) alloc(4);
    for 0..3  bytes[it] = cast(u8) (10 + it);
    a.other = bytes + 1;
    b.other = c;          // a *void to an object also reached typed
    return a;
}
RING :: #run make_ring();
main :: () {
    n := RING;
    for 1..7 { print("% ", n.value); n = n.next; }
    print("\n% % %\n", RING.next.next.next == RING, (cast(*u8) RING.other).*, (cast(*Node) RING.next.other) == RING.next.next);
    RING.next.value = 20;  // the baked objects are writable and shared
    print("%\n", RING.next.next.next.next.value);
}
