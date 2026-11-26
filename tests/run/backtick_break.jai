#import "Basic";

skip_odd :: (x: int) #expand {
    if x % 2 == 1  `continue;
}

stop_at :: (x: int, limit: int) #expand {
    defer print("(checked %) ", x);
    // The macro's own loop: a plain break would leave it, `break leaves the caller's.
    for 1..3 {
        if x >= limit  `break;
        if it == 2  break;
    }
}

main :: () {
    for 0..9 {
        skip_odd(it);
        print("% ", it);
    }
    print("\n");

    i := 0;
    while true {
        stop_at(i, 3);
        print("i=% ", i);
        i += 1;
    }
    print("\ndone at %\n", i);

    N :: #run () -> int {
        s := 0;
        for 1..10 { skip_odd(it); s += it; }
        return s;
    }();
    print("%\n", N);
}
