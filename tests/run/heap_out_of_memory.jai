// Running out of memory stops the program with a message (on stderr) and exit status 1.
// EXIT: 1
#import "Basic";

main :: () {
    print("allocating too much\n");
    p := alloc(1 << 60);
    print("not reached %\n", p);
}
