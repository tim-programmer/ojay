// Threads (Linux: POSIX threads through the C library).
// SKIP: static
// SKIP: arm32
// SKIP: riscv
#import "Basic";
#import "Thread";

Work :: struct {
    mutex: Mutex;
    total: s64;
    atomic_total: s64;
}

worker :: (thread: *Thread) -> s64 {
    work := cast(*Work) thread.data;
    for 1..10000 {
        lock(*work.mutex);
        work.total += 1;
        unlock(*work.mutex);
        atomic_add(*work.atomic_total, 2);
    }
    // Each thread has its own context and temporary storage.
    s := tprint("thread %", context.thread_index);
    return ifx context.temporary_storage == *thread.temporary_storage && s.count > 0 then thread.index else 0;
}

ping :: (thread: *Thread) -> s64 {
    sems := cast(*[2] Semaphore) thread.data;
    wait_for(*sems.*[0]);
    signal(*sems.*[1]);
    return 7;
}

main :: () {
    work: Work;
    init(*work.mutex);
    threads: [4] Thread;
    for * threads {
        thread_init(it, worker);
        it.data = *work;
    }
    for * threads  thread_start(it);
    for * threads  while !thread_is_done(it, 100) {}
    indices_ok := true;
    for threads  indices_ok = indices_ok && it.result == it.index && it.index > 0;
    print("% % %\n", work.total, work.atomic_total, indices_ok);
    print("main thread index %\n", context.thread_index);
    for * threads  thread_deinit(it);
    destroy(*work.mutex);

    sems: [2] Semaphore;
    init(*sems[0]);
    init(*sems[1]);
    t: Thread;
    thread_init(*t, ping);
    t.data = *sems;
    thread_start(*t);
    print("% (not signaled yet)\n", wait_for(*sems[1], 50));
    signal(*sems[0]);
    print("%\n", wait_for(*sems[1]));
    thread_is_done(*t, -1);
    print("result %\n", t.result);
    thread_deinit(*t);

    thread_yield();
    v: s64 = 1;
    print("% % %\n", compare_and_swap(*v, 1, 9), compare_and_swap(*v, 1, 2), v);
}
