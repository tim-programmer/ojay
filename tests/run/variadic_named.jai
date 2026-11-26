// Parameters after a variadic one (passed by name), and what they make
// possible in Basic: join, log / log_error with loc and flags, assert's loc.
#import "Basic";

sum :: (prefix: string, values: ..int, scale := 1, suffix := "") {
    total := 0;
    for values  total += it;
    print("%: % (% values)%\n", prefix, total * scale, values.count, suffix);
}

needs :: (values: ..int, k: int) -> int { return values.count * k; }

// Polymorphic, with a parameter after the variadic one.
pjoin :: (items: ..$T, sep := ", ") -> string {
    b: String_Builder;
    for items {
        if it_index  print_to_builder(*b, "%", sep);
        print_to_builder(*b, "%", it);
    }
    return builder_to_string(*b);
}

// Overloads: the variadic one only fits when `tag` is named.
pick :: (a: ..int, tag: string) -> int { return 1; }
pick :: (a: int, b: int) -> int { return 2; }

AT_COMPILE_TIME :: #run pjoin(1, 2, 3, sep = "-");

my_logger :: (message: string, data: *void, info: Log_Info) {
    print("[% line % flags % user %] %\n", info.source_identifier, info.location.line_number,
          cast(u32) info.common_flags, info.user_flags, message);
}

positive :: (x: int, loc := #caller_location) -> bool {
    assert(x > 0, "x must be positive", loc = loc);
    return true;
}

main :: () {
    sum("a", 1, 2, 3);
    sum("b", 1, 2, 3, scale = 10);
    sum("c", suffix = "!", scale = 2);
    arr := int.[4, 5];
    sum("d", ..arr, suffix = " (spread)");
    sum(prefix = "e", scale = 3);
    print("%\n", needs(1, 2, 3, k = 7));

    print("%\n", pjoin(1, 2, 3));
    print("%\n", pjoin(1.5, 2.5, sep = " | "));
    print("% %\n", pick(1, 2), pick(1, 2, tag = "x"));
    print("%\n", AT_COMPILE_TIME);

    print("[%]\n", join("a", "b", "c", separator = ", "));
    print("[%]\n", join("a", "b", separator = "/", before_first = true, after_last = true));
    print("[%] [%] [%]\n", join(), join("solo"), join("", ""));
    parts := string.["x", "y"];
    print("[%]\n", join(..parts, separator = "+"));
    #run assert(join("p", "q", separator = "-") == "p-q");

    context.logger = my_logger;
    context.log_source_identifier = "app";
    log("hello %", 42);
    log("warn", flags = .WARNING, user_flags = 7);
    log_error("bad % %", 1, 2, user_flags = 3);

    print("%\n", positive(5));
    #run positive(1);
}
