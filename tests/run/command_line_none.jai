// Without arguments, a bare-metal program gets at most its name (from the
// debugger or simulator, through semihosting; none without semihosting).
// SKIP: x86_64
#import "Basic";

V :: #run get_command_line_arguments().count;  // none at compile time

main :: () {
    args := get_command_line_arguments();
    print("at most the name: % %\n", args.count <= 1, V);
    if args.count == 1  print("name ends in .exe: %\n", ends_with_exe(args[0]));
    else                print("name ends in .exe: true\n");
}

ends_with_exe :: (s: string) -> bool {
    return s.count > 4 && s[s.count - 4] == #char "." && s[s.count - 3] == #char "e";
}
