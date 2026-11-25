#import "Basic";

classify :: (n: int) -> string {
    if n < 0 return "negative";
    if n == 0 { return "zero"; }
    else if n < 10 { return "small"; }
    return "large";
}

main :: () {
    print("% % % %\n", classify(-5), classify(0), classify(7), classify(100));

    i := 0;
    while i < 5 {
        i += 1;
        if i == 2 continue;
        if i == 4 break;
        print("while %\n", i);
    }

    for 0..4 {
        if it == 1 continue;
        if it == 3 break;
        print("for %\n", it);
    }

    for < 1..3 print("rev %\n", it);
    for i: 5..7 print("named %\n", i);

    // empty range does not execute
    for 5..4 print("never\n");

    // nested loops with labeled break
    for outer: 0..2 {
        for inner: 0..2 {
            if inner == 2 continue outer;
            if outer == 2 break outer;
            print("(%,%) ", outer, inner);
        }
    }
    print("\n");

    x := ifx i > 3 then 100 else 200;
    y := ifx i > 10 "big" else "small";
    print("ifx: % %\n", x, y);

    // switch
    for 0..4 {
        if it == {
            case 0; print("zero ");
            case 1; #through;
            case 2; print("one-or-two ");
            case; print("other ");
        }
    }
    print("\n");
}
