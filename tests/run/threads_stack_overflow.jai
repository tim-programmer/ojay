// A thread that overflows its stack stops the program (Cortex-M33: the stack
// limit register faults, exit status 134, with a message on stderr).
// SKIP: x86_64
// SKIP: riscv
// EXIT: 134
#import "Basic";
#import "Thread";
#import "Hardware";

// 100000 frames of over 256 bytes: far more than 2 KB (the volatile stores keep
// each frame's buffer, so the optimizer cannot turn this into a loop).
down :: (n: int) -> int {
    buffer: [32] int;
    volatile_store(*buffer[n % 32], n);
    if n == 100000  return 0;
    return down(n + 1) + volatile_load(*buffer[n % 32]);
}

deep :: (thread: *Thread) -> s64 { return down(0); }

main :: () {
    t: Thread;
    thread_init(*t, deep, stack_size = 2048);
    thread_start(*t);
    print("started\n");
    thread_is_done(*t, -1);
    print("not reached\n");
}
