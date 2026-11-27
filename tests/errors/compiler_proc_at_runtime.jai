// ERROR: 'generate' is a #compiler procedure: it only runs at compile time
#import "Basic";
generate :: (n: int) -> string #compiler { return tprint("x := %;", n); }
helper :: () -> string { return generate(3); }
main :: () { s := helper(); }
