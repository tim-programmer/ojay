// #insert,scope(): inserted code sees the names at the #insert (a macro's
// locals) and, behind them, the names where it was written.
// #insert,scope(target): it is checked where Code `target` was written.
#import "Basic";

with_square :: (n: int, body: Code) #expand {
    square := n * n;
    #insert,scope() body;
}

// `here` is written in the macro: the body sees the macro's `local` (and
// what the macro's declaration sees), not the caller's names.
with_local :: (body: Code) #expand {
    local := 100;
    here :: #code 0;
    #insert,scope(here) body;
}

twice :: (body: Code, where: Code) #expand {
    #insert,scope(where) body;
    #insert,scope(where) body;
}

LIMIT :: 3;

main :: () {
    total := 0;
    with_square(7, #code { print("square %\n", square); total += square; });
    twice(#code { total += 1; }, #code 0);
    print("total %\n", total);
    with_local(#code print("local % limit %\n", local, LIMIT));
    v := 5;
    print("%\n", #insert,scope() #code v * 2);
}
