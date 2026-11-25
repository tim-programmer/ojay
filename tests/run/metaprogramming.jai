// Metaprogramming: #code, #insert, #expand macros and #modify. The same code
// also runs at compile time (#run), where the interpreter executes the
// expanded and inserted code.
// COMPILE_OUTPUT: compile time: unrolled 30, macros 352, collect 12
#import "Basic";

// ---- #insert of strings made by #run

// An unrolled loop: one statement per element.
unrolled_source :: (n: int) -> string {
    s := "";
    for i: 0..n-1  s = sprint("%total += values[%] * %;\n", s, i, i + 1);
    return s;
}
weighted_sum :: (values: [4]int) -> int {
    total := 0;
    #insert #run unrolled_source(4);
    return total;
}

// Struct members.
member_source :: (names: []string) -> string {
    s := "";
    for names  s = sprint("% %: int = %;\n", s, it, (it_index + 1) * 10);
    return s;
}
Generated :: struct {
    id: int;
    #insert #run member_source(.["alpha", "beta", "gamma"]);
}

// Top-level declarations.
#insert #run "SCALE :: 3; scaled :: (v: int) -> int { return v * SCALE; }";

// An inserted expression.
answer :: () -> int { return #insert "6 * 7"; }

// ---- #code

// Code constants, inserted as a statement (its declaration is visible after
// the #insert) and as an expression.
DECLARE_LIMIT :: #code { limit := 5; };
DOUBLE_LIMIT :: #code limit * 2;

// Code passed to a procedure as a compile-time parameter: each different
// piece of code gets its own instance, with the code inserted in its body.
apply :: (x: int, $op: Code) -> int {
    k := 3;
    return #insert op;
}

// ---- #expand macros

square :: (x: int) -> int #expand {
    return x * x;  // leaves the expansion with a value
}

// Reads and assigns a variable of the caller.
bump :: (amount: int) #expand {
    `counter += amount;
}

// Names in the body mean what they mean here; `name is the caller's.
helper :: () -> int { return 1; }
both_helpers :: () -> int #expand { return `helper() * 100 + helper(); }

// `return returns from the procedure the macro was expanded in.
return_if_negative :: (x: int) #expand {
    if x < 0 `return -1;
}
checked_double :: (x: int) -> int {
    return_if_negative(x);
    return x * 2;
}

// In a macro expanded by another macro, `return leaves the outer expansion.
first_big :: (x: int) -> int #expand {
    leave_if_big(x);
    leave_if_big(x * 2);
    return 0;
}
leave_if_big :: (x: int) #expand {
    if x > 100 `return x;
}
classify :: (x: int) -> int {
    leave_if_big(x);
    return first_big(x);
}

// A Code argument inserted in the macro: it is checked where the call is,
// so it sees the caller's variables and the `elem the macro declares there.
// break and continue in it apply to the macro's loop.
each_even :: (arr: []int, body: Code) #expand {
    `elem := 0;
    i := 0;
    while i < arr.count {
        defer i += 1;
        if arr[i] % 2 == 1  continue;
        `elem = arr[i];
        #insert body;
    }
}
collect_evens :: () -> int {
    total := 0;
    values := int.[1, 2, 4, 5, 6, 8, 10];
    each_even(values, #code {
        if elem > 6  break;
        total += elem;
    });
    return total;  // 2 + 4 + 6
}

// `defer: runs when the caller's scope ends.
traced :: (name: string) #expand {
    print("enter %\n", name);
    `defer print("leave %\n", name);
}

// Recursion that ends: a compile-time parameter and #if.
count_down :: ($n: int) #expand {
    #if n > 0 {
        print("% ", n);
        count_down(n - 1);
    } else {
        print("liftoff\n");
    }
}

// continue in a macro body applies to the caller's loop.
skip_odd :: (v: int) #expand {
    if v % 2 == 1  continue;
}

macro_results :: () -> int {
    r := square(4) + square(3);                  // 25
    r += classify(60) + classify(200);           // 120 + 200
    r += checked_double(-5) + checked_double(4); // -1 + 8
    return r;                                    // 352
}

// ---- #modify

// Rejects floating-point types, so the other overload is used for them.
describe :: (x: $T) -> string #modify {
    return T != float32 && T != float64, "floats are handled by another overload";
} {
    return "integer";
}
describe :: (x: float64) -> string { return "float"; }

// Rewrites T: small integer types are added as s64.
add :: (a: $T, b: T) -> T #modify {
    if T == s8 || T == s16 || T == s32  T = s64;
    return true;
} {
    return a + b;
}

compile_time_report :: () {
    w := weighted_sum(.[1, 2, 3, 4]);
    print("compile time: unrolled %, macros %, collect %\n", w, macro_results(), collect_evens());
}
#run compile_time_report();

main :: () {
    print("unrolled: %\n", weighted_sum(.[1, 2, 3, 4]));
    g: Generated;
    print("generated: % % % %\n", g.id, g.alpha, g.beta, g.gamma);
    print("top level: % %\n", SCALE, scaled(14));
    print("answer: %\n", answer());

    #insert DECLARE_LIMIT;
    print("limit: % %\n", limit, #insert DOUBLE_LIMIT);
    print("apply: % %\n", apply(4, #code x * k), apply(4, #code x + k + 100));

    print("square: %\n", square(9));
    counter := 1;
    bump(10);
    bump(counter);
    print("counter: %\n", counter);
    helper :: () -> int { return 7; }
    print("helpers: %\n", both_helpers());
    print("checked_double: % %\n", checked_double(-3), checked_double(21));
    print("classify: % % %\n", classify(10), classify(60), classify(500));
    print("collect_evens: %\n", collect_evens());
    {
        traced("block");
        print("inside\n");
    }
    count_down(3);
    for 1..6 {
        skip_odd(it);
        print("% ", it);
    }
    print("\n");
    print("macro_results: %\n", macro_results());
    MACROS_AT_COMPILE_TIME :: #run macro_results();
    print("at compile time: %\n", MACROS_AT_COMPILE_TIME);

    print("describe: % % %\n", describe(3), describe(2.5), describe(cast(u8) 1));
    p, q: s16 = 30000, 30000;
    print("add: % (%), % (%)\n", add(p, q), type_of(add(p, q)), add(1, 2), type_of(add(1, 2)));
}
