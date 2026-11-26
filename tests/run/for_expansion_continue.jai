#import "Basic";

Range :: struct { n: int; }
for_expansion :: (r: *Range, body: Code, flags: For_Flags) #expand {
    for i: 0..r.n-1 {
        `it := i;
        `it_index := i;
        defer print("[after %] ", i);
        #insert body;
    }
}

Pairs :: struct { n: int; }
for_expansion :: (p: *Pairs, body: Code, flags: For_Flags) #expand {
    i := 0;
    while i < p.n {
        `it := i * 2;
        `it_index := i;
        i += 1;
        #insert body;
    }
}

main :: () {
    r := Range.{4};
    for outer: r {
        for 1..3 {
            if outer % 2 == 1  continue outer;
            print("% ", outer * 10 + it);
        }
        print("| ");
    }
    print("\n");

    p := Pairs.{5};
    count := 0;
    for x: p {
        for y: r {
            if y == 1  continue x;
            count += 1;
        }
        print("not reached\n");
    }
    print("\ncount %\n", count);

    // A #run loop too.
    TOTAL :: #run () -> int {
        q := Pairs.{4};
        s := 0;
        for v: q {
            for 0..10  { if it == 2  continue v; s += v; }
        }
        return s;
    }();
    print("%\n", TOTAL);
}
