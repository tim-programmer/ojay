#import "Basic";

primes :: (n: int) -> [] int {
    out: [..] int;
    candidate := 2;
    while out.count < n {
        is_prime := true;
        for out  if candidate % it == 0 { is_prime = false; break; }
        if is_prime  array_add(*out, candidate);
        candidate += 1;
    }
    return out;
}

PRIMES :: #run primes(10);

Node :: struct { value: int; next: *Node; }
LIST :: #run () -> *Node {
    head: *Node;
    for 1..3 {
        n := New(Node);
        n.value = it * 10;
        n.next = head;
        head = n;
    }
    return head;
}();

Op :: struct { name: string; apply: (int) -> int; }
double :: (x: int) -> int { return x * 2; }
OPS :: #run () -> [] Op {
    ops: [..] Op;
    array_add(*ops, .{"double", double});
    array_add(*ops, .{"square", (x: int) -> int { return x * x; }});
    return ops;
}();

WORDS :: #run () -> [..] string {
    w: [..] string;
    array_add(*w, "alpha");
    array_add(*w, tprint("beta%", 2));
    return w;
}();

ANSWER :: #run () -> Any { x := New(int); x.* = 42; return x.*; }();

// A pointer into the middle of a block keeps the whole block.
MIDDLE :: #run () -> *int {
    a := NewArray(5, int);
    for * a  it.* = it_index * 100;
    return *a[2];
}();

// A later #run sees the baked values.
SUM :: #run () -> int { s := 0; for PRIMES s += it; return s; }();

main :: () {
    print("% %\n", PRIMES, SUM);
    n := LIST;
    while n { print("% ", n.value); n = n.next; }
    print("\n");
    for OPS  print("% %\n", it.name, it.apply(7));
    words := WORDS;
    array_add(*words, "gamma");  // copies the baked items into new memory
    print("% % %\n", words, WORDS.count, words.allocated >= 3);
    array_free(words);
    print("% %\n", ANSWER, ANSWER.type);
    print("% % %\n", MIDDLE.*, MIDDLE[-2], MIDDLE[2]);
    p := PRIMES;
    p[0] = 99;  // the baked array is writable (it is one global)
    print("%\n", PRIMES[0]);
}
