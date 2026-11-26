// Bare-metal threads: a wait that nothing can end stops the program.
// SKIP: x86_64
// SKIP: uart
// EXIT: 1
#import "Basic";
#import "Thread";

m: Mutex;

taker :: (thread: *Thread) -> s64 {
    print("thread waits for the lock\n");
    lock(*m);
    print("not reached\n");
    return 0;
}

main :: () {
    init(*m);
    lock(*m);
    t: Thread;
    thread_init(*t, taker);
    thread_start(*t);
    print("timed wait %\n", thread_is_done(*t, 10));
    thread_is_done(*t, -1);
    print("not reached\n");
}
