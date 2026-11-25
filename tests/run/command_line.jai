// get_command_line_arguments() on hosted / freestanding Linux.
// ARGS: one "two words" 3 ""
// SKIP: arm32
// SKIP: riscv
#import "Basic";

main :: () {
    args := get_command_line_arguments();
    print("count: %\n", args.count);
    exe := args[0];
    print("program name ends in .exe: %\n", exe.count > 4 && exe[exe.count - 4] == #char ".");
    for args {
        if it_index == 0  continue;
        print("[%] '%' (% bytes)\n", it_index, it, it.count);
    }
    again := get_command_line_arguments();
    print("same array: %\n", again.data == args.data);
}
