// ERROR: steps; is there an infinite loop?
collatz_forever :: () -> int {
    n := 27;
    while n != 0 {   // never 0: 1 -> 4 -> 2 -> 1 -> ...
        if n % 2 == 0  n /= 2;
        else           n = 3 * n + 1;
    }
    return n;
}
X :: #run collatz_forever();
