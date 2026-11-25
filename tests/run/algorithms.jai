#import "Basic";

// Classic algorithms over arrays and integers.

gcd :: (a: int, b: int) -> int {
    while b != 0 {
        t := b;
        b = a % b;
        a = t;
    }
    return a;
}

insertion_sort :: (xs: []int) {
    for i: 1..xs.count-1 {
        key := xs[i];
        j := i - 1;
        while j >= 0 && xs[j] > key {
            xs[j + 1] = xs[j];
            j -= 1;
        }
        xs[j + 1] = key;
    }
}

binary_search :: (xs: []int, target: int) -> found: bool, index: int {
    lo := 0;
    hi := xs.count - 1;
    while lo <= hi {
        mid := lo + (hi - lo) / 2;
        if xs[mid] == target return true, mid;
        if xs[mid] < target lo = mid + 1;
        else hi = mid - 1;
    }
    return false, -1;
}

collatz_steps :: (n: u64) -> int {
    steps := 0;
    while n != 1 {
        if n % 2 == 0 n /= 2;
        else n = 3 * n + 1;
        steps += 1;
    }
    return steps;
}

main :: () {
    print("gcd(1071, 462) = %\n", gcd(1071, 462));
    print("gcd(17, 5) = %\n", gcd(17, 5));

    data := int.[38, 27, 43, 3, 9, 82, 10, -5, 0, 27];
    insertion_sort(data);
    print("sorted: %\n", data);

    for target: int.[43, 27, -5, 100] {
        found, index := binary_search(data, target);
        if found print("% found at %\n", target, index);
        else     print("% not found\n", target);
    }

    // Sieve of Eratosthenes
    N :: 60;
    composite: [N + 1]bool;
    primes: [..]int;
    for i: 2..N {
        if composite[i] continue;
        array_add(*primes, i);
        j := i * i;
        while j <= N {
            composite[j] = true;
            j += i;
        }
    }
    print("% primes up to %: %\n", primes.count, N, primes);

    // Fibonacci in u64: fib(93) is the largest that fits.
    a: u64 = 0;
    b: u64 = 1;
    for 1..93 {
        c := a + b;
        a = b;
        b = c;
    }
    print("fib(93) = %\n", a);

    print("collatz(27) takes % steps\n", collatz_steps(27));

    // Integer square root by Newton's method
    isqrt :: (n: s64) -> s64 {
        if n < 2 return n;
        x := n;
        y := (x + 1) / 2;
        while y < x {
            x = y;
            y = (x + n / x) / 2;
        }
        return x;
    }
    print("isqrt: % % % %\n", isqrt(0), isqrt(99), isqrt(100), isqrt(1_000_000_007));
}
