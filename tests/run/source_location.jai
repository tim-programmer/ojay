// #location(x) and #caller_location: Source_Code_Location values (file, line,
// column) for declarations and for the calls that leave a
// `loc := #caller_location` argument out. Paths differ between machines, so
// only the file's base name is printed (and checked to be absolute).
// COMPILE_OUTPUT: poly instance s64
// COMPILE_OUTPUT: #run: source_location.jai:108:51 absolute source_location.jai:9:1 absolute source_location.jai:33:1 absolute line 107 line 49
#import "Basic";

Point :: struct { x, y: int; }

counter := 0;

LIMIT :: 10;

base_name :: (s: string) -> string {
    i := s.count - 1;
    while i >= 0 && s[i] != #char "/"  i -= 1;
    r: string;
    r.data = s.data + i + 1;
    r.count = s.count - i - 1;
    return r;
}

describe :: (loc: Source_Code_Location) -> string {
    absolute := loc.fully_pathed_filename.count > 0 && loc.fully_pathed_filename[0] == #char "/";
    return tprint("%:%:% %", base_name(loc.fully_pathed_filename), loc.line_number, loc.character_number,
                  ifx absolute then "absolute" else "NOT ABSOLUTE");
}

where :: (loc := #caller_location) -> Source_Code_Location { return loc; }

// Named arguments and a default before the location.
tagged :: (tag: string, n := 1, loc := #caller_location) -> string {
    return tprint("% % line %", tag, n, loc.line_number);
}

// Overloads: both take a location.
show :: (x: int, loc := #caller_location) -> string { return tprint("int % line %", x, loc.line_number); }
show :: (x: string, loc := #caller_location) -> string { return tprint("string % line %", x, loc.line_number); }

// A polymorphic procedure: the location is an argument, so the two calls of
// it with int share one instance (#modify runs once per instance).
poly :: (x: $T, loc := #caller_location) -> T, int #modify { print("poly instance %\n", T); return true; } {
    return x, loc.line_number;
}

// A macro whose body calls `where`: the call is the macro's own line.
in_macro :: () -> int #expand {
    return where().line_number;
}

// A macro with its own location parameter: the line of the macro call.
macro_loc :: (loc := #caller_location) -> int #expand {
    return loc.line_number;
}

Three :: struct {}
for_expansion :: (t: Three, body: Code, flags: For_Flags) #expand {
    for i: 0..2 {
        `it := i;
        `it_index := i;
        line := where().line_number;  // the for_expansion's own line
        if i == 0  print("for_expansion line %\n", line);
        #insert body;
    }
}

main :: () {
    print("procedure %\n", describe(#location(describe)));
    print("struct %\n", describe(#location(Point)));
    print("global %\n", describe(#location(counter)));
    print("constant %\n", describe(#location(LIMIT)));
    local := 5;
    print("local %\n", describe(#location(local)));
    print("expression %\n", describe(#location(local + 1)));
    print("is #file: %\n", #location(main).fully_pathed_filename.count >= #file.count);

    print("direct %\n", describe(where()));
    print("explicit %\n", where(loc = #location(Point)).line_number);
    explicit: Source_Code_Location;
    explicit.line_number = 1234;
    print("explicit value %\n", where(explicit).line_number);
    print("%\n", tagged("a"));
    print("%\n", tagged("b", n = 2));
    print("%\n", tagged(n = 3, tag = "c"));
    print("%\n", show(7));
    print("%\n", show("x"));
    v1, l1 := poly(1);
    v2, l2 := poly(2);
    print("poly % % % %\n", v1, l1, v2, l2);
    print("macro body line %\n", in_macro());
    print("macro call line %\n", macro_loc());
    for Three.{}  if it == 1  print("body line %\n", where().line_number);

    // A procedure value has no defaults: the location is passed explicitly.
    p := where;
    print("procedure value %\n", p(#location(Point)).line_number);

    // Inserted text reports the #insert; inserted #code the line it was written on.
    #insert "print(\"inserted string %\\n\", describe(where()));";
    #insert INSERTED;
}

INSERTED :: #code { print("inserted code %\n", where().line_number); }

at_compile_time :: () {
    v, l := poly(3);
    print("#run: % % % line % line %\n", describe(where()), describe(#location(Point)),
          describe(#location(tagged)), l, in_macro());
}
#run at_compile_time();
