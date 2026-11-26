// #this: the struct whose body it is in (the instance, in a polymorphic
// struct), or the procedure whose body it is in.
// COMPILE_OUTPUT: run: 3 nodes, sum 6, List(s32) self true, fact 120
#import "Basic";

Node :: struct {
    value: int;
    next: *#this;
}

List :: struct ($T: Type) {
    Self :: #this;
    value: T;
    next: *#this;
    prepend :: (list: *#this, v: T) -> *#this {
        n := New(Self);  // in the body, #this would be prepend itself
        n.value = v;
        n.next = list;
        return n;
    }
}

factorial :: (n: int) -> int {
    if n <= 1  return 1;
    return n * #this(n - 1);
}

walk :: () -> string {
    a := Node.{1, null};
    b := Node.{2, *a};
    c := Node.{3, *b};
    count, sum := 0, 0;
    p := *c;
    while p {
        count += 1;
        sum += p.value;
        p = p.next;
    }
    return sprint("% nodes, sum %, % self %, fact %", count, sum, List(s32), List(s32).Self == List(s32),
                  factorial(5));
}

#run print("run: %\n", walk());

main :: () {
    print("%\n", walk());
    print("type_of(Node.next) = %\n", type_of(Node.{}.next));

    l: *List(float) = null;
    l = List(float).prepend(l, 1.5);
    l = List(float).prepend(l, 2.5);
    for 0..1 {
        print("% ", l.value);
        l = l.next;
    }
    print("\n");
    print("List(int).Self = %, List(float).Self = %\n", List(int).Self, List(float).Self);
    f := #this;  // main itself
    print("f == main: %, factorial(6) = %\n", f == main, factorial(6));
}
