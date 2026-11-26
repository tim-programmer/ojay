#import "Basic";

divmod :: (a: int, b: int) -> int, int #expand {
    if b == 0  return 0, 0;
    return a / b, a % b;
}

min_max :: (values: [] int) -> (lo: int, hi: int) #expand {
    lo, hi := values[0], values[0];
    for values {
        if it < lo  lo = it;
        if it > hi  hi = it;
    }
    return lo, hi;
}

forward :: (a: int, b: int) -> int, int #expand { return divmod(a, b); }

find :: (values: [] int, x: int) -> bool, int #expand {
    for values  if it == x  return true, it_index;
    return false, -1;
}

main :: () {
    q, r := divmod(17, 5);
    print("% %\n", q, r);
    lo, hi := min_max(.[4, -2, 9, 3]);
    print("% %\n", lo, hi);
    q, r = forward(9, 0);
    print("% %\n", q, r);
    first := divmod(20, 6);  // only the first value
    print("%\n", first);
    found, at := find(.[5, 6, 7], 7);
    print("% %\n", found, at);

    S :: #run () -> int {
        a, b := divmod(29, 4);
        return a * 100 + b;
    }();
    print("%\n", S);
}
