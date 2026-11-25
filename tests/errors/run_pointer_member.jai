// ERROR: the result's next (of type *Node) is a pointer into compile-time memory
#import "Basic";
Node :: struct { value: int; next: *Node; }
make_list :: () -> Node {
    n: Node;
    n.value = 1;
    n.next = New(Node);
    return n;
}
LIST :: #run make_list();
