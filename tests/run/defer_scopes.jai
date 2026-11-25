#import "Basic";

f :: () -> int {
    defer print("f: deferred 1\n");
    defer print("f: deferred 2\n");
    {
        defer print("f: inner block\n");
        print("f: in block\n");
    }
    for 1..3 {
        defer print("f: end of iteration %\n", it);
        if it == 2 continue;
        if it == 3 break;
        print("f: body %\n", it);
    }
    return 42;
}

main :: () {
    r := f();
    print("f returned %\n", r);
    x := 1;
    {
        x := 2;   // shadowing in a nested scope
        print("inner x = %\n", x);
    }
    print("outer x = %\n", x);
}
