// Bare-metal programs have no command-line arguments.
// SKIP: x86_64
#import "Basic";

V :: #run get_command_line_arguments().count;  // none at compile time either

main :: () {
    args := get_command_line_arguments();
    print("count: % %\n", args.count, V);
    for args  print("unexpected: %\n", it);
}
