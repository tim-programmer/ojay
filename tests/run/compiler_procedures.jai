// #compiler procedures: they run only at compile time (here from #run and
// #insert), and other #compiler procedures may call them.
// COMPILE_OUTPUT: table of 5 squares
#import "Basic";

squares_text :: (n: int) -> string #compiler {
    b: String_Builder;
    append(*b, "SQUARES :: int.[");
    for 0..n-1 {
        if it  append(*b, ", ");
        print_to_builder(*b, "%", square(it));
    }
    append(*b, "];");
    print("table of % squares\n", n);
    return builder_to_string(*b);
}
square :: (x: int) -> int #compiler { return x * x; }

#insert #run squares_text(5);

main :: () {
    print("%\n", SQUARES);
    print("%\n", #run square(12));
}
