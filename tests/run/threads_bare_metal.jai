// Threads on bare metal: cooperative tasks, each with its own stack and
// context, switched by thread_yield and the waits.
// SKIP: x86_64
#import "Basic";
#import "Thread";

order: [..] u8;

stepper :: (thread: *Thread) -> s64 {
    letter := (cast(*u8) thread.data).*;
    // Floats live in callee-saved registers across the switches.
    f: float32 = 0.5;
    g: float64 = 0.25;
    for 1..3 {
        array_add(*order, letter);
        f *= 2;
        g *= 2;
        thread_yield();
    }
    s := tprint("%:%", context.thread_index, context.user_index);
    ok := context.temporary_storage == *thread.temporary_storage;
    return ifx ok && s == tprint("%:7", thread.index) && f == 4 && g == 2 then 100 + cast(s64) thread.index else -1;
}

Shared :: struct {
    mutex: Mutex;
    in_section: s64;
    overlaps: s64;
    total: s64;
    items: Semaphore;
    done_signal: Semaphore;
}

locker :: (thread: *Thread) -> s64 {
    sh := cast(*Shared) thread.data;
    for 1..5 {
        lock(*sh.mutex);
        sh.in_section += 1;
        if sh.in_section > 1  sh.overlaps += 1;
        thread_yield();  // the others run while this one holds the lock
        sh.total += 1;
        sh.in_section -= 1;
        unlock(*sh.mutex);
        thread_yield();
    }
    return 0;
}

consumer :: (thread: *Thread) -> s64 {
    sh := cast(*Shared) thread.data;
    got := 0;
    for 1..4 {
        wait_for(*sh.items);
        got += 1;
    }
    signal(*sh.done_signal);
    return got;
}

depth :: (n: int) -> int {
    buffer: [16] s32;
    for * buffer  it.* = xx n;
    if n == 0  return 0;
    return buffer[3] + depth(n - 1);
}

deep :: (thread: *Thread) -> s64 {
    thread_yield();
    return depth(100);
}

main :: () {
    threads: [3] Thread;
    new_context := context;
    new_context.user_index = 7;
    push_context new_context {
        for * threads {
            thread_init(it, stepper);
            it.data = "abc".data + it_index;
            thread_start(it);
        }
    }
    // Main keeps its own context while the threads run.
    push_context new_context {
        context.user_index = 3;
        while !thread_is_done(*threads[2]) {
            array_add(*order, #char "m");
            thread_yield();
        }
        print("main user_index %\n", context.user_index);
    }
    trace: string;
    trace.data = order.data;
    trace.count = order.count;
    print("%\n", trace);
    for threads  print("result %\n", it.result);
    for * threads  thread_deinit(it);
    print("main thread index % user_index %\n", context.thread_index, context.user_index);

    sh: Shared;
    init(*sh.mutex);
    init(*sh.items);
    init(*sh.done_signal);
    lockers: [3] Thread;
    for * lockers {
        thread_init(it, locker);
        it.data = *sh;
        thread_start(it);
    }
    c: Thread;
    thread_init(*c, consumer);
    c.data = *sh;
    thread_start(*c);
    for 1..4  signal(*sh.items);
    print("done signaled %\n", wait_for(*sh.done_signal));
    for * lockers  thread_is_done(it, -1);
    print("total % overlaps % consumed %\n", sh.total, sh.overlaps, ifx thread_is_done(*c, -1) then c.result else -1);
    // Nothing else can signal it: the wait gives up.
    print("timed out wait %\n", wait_for(*sh.items, 100));
    for * lockers  thread_deinit(it);
    thread_deinit(*c);

    d: Thread;
    thread_init(*d, deep, stack_size = 16384);
    thread_start(*d);
    thread_is_done(*d, -1);
    print("deep %\n", d.result);
    thread_deinit(*d);

    v: s64 = 5;
    print("% % %", atomic_add(*v, 10), atomic_swap(*v, 1), v);
    print(" % % %\n", compare_and_swap(*v, 1, 9), compare_and_swap(*v, 1, 2), v);
}
