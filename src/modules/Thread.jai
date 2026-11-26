// Module Thread: threads, mutexes and semaphores, after Jai's module of the
// same name. Importing it makes the context per thread: each thread starts in
// a copy of the context thread_init was called in, with its own temporary
// storage and thread_index.
//
// On Linux the threads are POSIX threads (through the C library). On bare
// metal (OS == .NONE) they are cooperative tasks on the one core: a thread
// runs until it yields (thread_yield, or waiting in thread_is_done, lock or
// wait_for), and the others take turns in the order they were started. Each
// has a stack of its own (Thread.stack_size bytes, from the allocator
// thread_init was called with); the runtime switches stacks and the current
// context with them.

#import "Basic";

#assert OS == .LINUX || OS == .NONE, "the Thread module needs Linux (POSIX threads) or bare metal (cooperative tasks)";

Thread_Proc :: #type (thread: *Thread) -> s64;

// The stack size bare-metal threads get unless thread_init is told otherwise.
DEFAULT_THREAD_STACK_SIZE :: 4096;

#if OS == .LINUX {

Thread :: struct {
    proc: Thread_Proc;
    data: *void;              // for the procedure
    index: u32;               // its context.thread_index
    result: s64;              // what proc returned (once it is done)

    starting_context: Context;
    temporary_storage: Temporary_Storage;
    stack_size: s64;          // used on bare metal only
    handle: u64;              // pthread_t
    started: bool;
    joined: bool;
}

} else {

Thread :: struct {
    proc: Thread_Proc;
    data: *void;              // for the procedure
    index: u32;               // its context.thread_index
    result: s64;              // what proc returned (once it is done)

    starting_context: Context;
    temporary_storage: Temporary_Storage;
    stack_size: s64;          // bytes of stack (set by thread_init; may be changed before thread_start)
    stack: *u8;               // allocated by thread_start, freed by thread_deinit
    saved_sp: *void;          // the stack pointer while it is not running
    next: *Thread;            // the next one in the run ring
    started: bool;
    done: bool;
    waiting: bool;            // in thread_is_done, lock or wait_for
}

}

// Prepares `thread` to run `proc` (thread_start starts it). The thread's
// context is a copy of the current one, with its own temporary storage.
// `stack_size` matters on bare metal only (Linux threads get the system's
// default stack).
thread_init :: (thread: *Thread, proc: Thread_Proc, temporary_storage_size: s32 = 16384,
                stack_size: s32 = DEFAULT_THREAD_STACK_SIZE) -> bool {
    thread.* = .{};
    thread.proc = proc;
    thread.index = cast(u32) atomic_add(*next_thread_index, 1);
    thread.starting_context = context;
    thread.temporary_storage.page_size = temporary_storage_size;
    thread.starting_context.temporary_storage = *thread.temporary_storage;
    thread.starting_context.thread_index = thread.index;
    thread.stack_size = stack_size;
    return true;
}

// ------------------------------------------------------------------ atomics

#if CPU == .X64 {

// Adds `delta` to the s64 at `p` as one indivisible step; returns the old value.
atomic_add :: (p: *s64, delta: s64) -> s64 {
    v := delta;
    #asm { lock xadd qword ptr [p], v }
    return v;
}

// Stores `value` at `p` as one indivisible step; returns the old value.
atomic_swap :: (p: *s64, value: s64) -> s64 {
    v := value;
    #asm { xchg qword ptr [p], v }
    return v;
}

} else {

// (The microcontrollers have no 64-bit atomic instructions: the runtime does
// these with interrupts masked.)
atomic_add :: (p: *s64, delta: s64) -> s64 { return __ojay_atomic_add64(p, delta); }
atomic_swap :: (p: *s64, value: s64) -> s64 { return __ojay_atomic_swap64(p, value); }

}

// Stores `new` at `p` if it holds `old`, as one indivisible step; returns
// whether it did.
compare_and_swap :: (p: *s64, old: s64, new: s64) -> bool {
    return __ojay_atomic_cas64(p, old, new) == old;
}

#if OS == .LINUX {

thread_start :: (thread: *Thread) {
    assert(!thread.started, "thread_start: the thread was started already");
    thread.started = true;
    r := pthread_create(*thread.handle, null, thread_entry, thread);
    assert(r == 0, "thread_start: pthread_create failed");
}

// Whether the thread has finished, waiting up to `milliseconds` for it
// (-1: until it finishes).
thread_is_done :: (thread: *Thread, milliseconds: s32 = 0) -> bool {
    if !thread.started  return false;
    if thread.joined  return true;
    r: s32;
    if milliseconds < 0 {
        r = pthread_join(thread.handle, null);
    } else if milliseconds == 0 {
        r = pthread_tryjoin_np(thread.handle, null);
    } else {
        deadline := deadline_after(milliseconds);
        r = pthread_timedjoin_np(thread.handle, null, *deadline);
    }
    if r == 0  thread.joined = true;
    return thread.joined;
}

// Waits for the thread (if it was started) and frees what it holds.
thread_deinit :: (thread: *Thread) {
    if thread.started && !thread.joined  thread_is_done(thread, -1);
    __free_temporary_storage(*thread.temporary_storage);
}

// Lets other threads run (sched_yield).
thread_yield :: () { sched_yield(); }

// ------------------------------------------------------------------ mutexes and semaphores

Mutex :: struct {
    storage: [8] u64;  // pthread_mutex_t
}

init :: (m: *Mutex) { pthread_mutex_init(m, null); }
destroy :: (m: *Mutex) { pthread_mutex_destroy(m); }
lock :: (m: *Mutex) { pthread_mutex_lock(m); }
unlock :: (m: *Mutex) { pthread_mutex_unlock(m); }

Semaphore :: struct {
    storage: [4] u64;  // sem_t
}

init :: (s: *Semaphore, initial_value: s32 = 0) { sem_init(s, 0, cast(u32) initial_value); }
destroy :: (s: *Semaphore) { sem_destroy(s); }
signal :: (s: *Semaphore, count: s32 = 1) { for 1..count  sem_post(s); }
// Waits until the semaphore is signaled (-1: as long as it takes). Returns whether it was.
wait_for :: (s: *Semaphore, milliseconds: s32 = -1) -> bool {
    if milliseconds < 0  return sem_wait(s) == 0;
    deadline := deadline_after(milliseconds);
    return sem_timedwait(s, *deadline) == 0;
}

} else {

// Adds the thread to the run ring; it first runs when the running thread
// next yields.
thread_start :: (thread: *Thread) {
    assert(!thread.started, "thread_start: the thread was started already");
    assert(thread.stack_size >= 1024, "thread_start: the stack size is too small (at least 1024 bytes)");
    start_scheduler();
    thread.stack = alloc(thread.stack_size, thread.starting_context.allocator);
    assert(thread.stack != null, "thread_start: no memory for the thread's stack");
    guard := cast(*u32) thread.stack;
    for i: 0..STACK_GUARD / 4 - 1  guard[i] = STACK_CANARY;
    thread.saved_sp = __ojay_task_stack_init(thread.stack + thread.stack_size, thread_entry, thread);
    thread.started = true;
    last := current;  // (the ring's end: the threads run in the order they were started)
    while last.next != current  last = last.next;
    thread.next = current;
    last.next = thread;
    events += 1;
}

// Whether the thread has finished, letting the others run first. With
// milliseconds != 0 it waits until the thread finishes. Bare metal has no
// clock here, so the time is not measured: the wait ends early (false) when
// no thread can make progress, and with -1 that is an error (a deadlock).
thread_is_done :: (thread: *Thread, milliseconds: s32 = 0) -> bool {
    if !thread.started  return false;
    return wait_until(thread_finished, thread, milliseconds, "thread_is_done");
}

// Waits for the thread (if it was started) and frees what it holds.
thread_deinit :: (thread: *Thread) {
    if thread.started && !thread.done  thread_is_done(thread, -1);
    if thread.stack  free(thread.stack, thread.starting_context.allocator);
    thread.stack = null;
    __free_temporary_storage(*thread.temporary_storage);
}

// Lets the other threads run (each once, in turn) before returning.
thread_yield :: () {
    if !current || current.next == current  return;
    from := current;
    if from != *main_thread  check_stack(from);
    current = from.next;
    __ojay_task_switch(*from.saved_sp, current.saved_sp, stack_limit(current));
}

// ------------------------------------------------------------------ mutexes and semaphores

Mutex :: struct {
    locked: bool;
}

init :: (m: *Mutex) { m.locked = false; }
destroy :: (m: *Mutex) {}
lock :: (m: *Mutex) {
    if m.locked  wait_until(mutex_free, m, -1, "lock");
    m.locked = true;
}
unlock :: (m: *Mutex) {
    m.locked = false;
    events += 1;
}

Semaphore :: struct {
    count: s64;
}

init :: (s: *Semaphore, initial_value: s32 = 0) { s.count = initial_value; }
destroy :: (s: *Semaphore) {}
signal :: (s: *Semaphore, count: s32 = 1) {
    atomic_add(*s.count, count);  // (interrupt handlers may signal)
    events += 1;
}
// Waits until the semaphore is signaled (-1: as long as it takes). Returns
// whether it was. As in thread_is_done, the time is not measured.
wait_for :: (s: *Semaphore, milliseconds: s32 = -1) -> bool {
    if !wait_until(semaphore_signaled, s, milliseconds, "wait_for")  return false;
    atomic_add(*s.count, -1);
    return true;
}

}

#scope_file

next_thread_index: s64 = 1;  // 0 is the main thread

#if OS == .LINUX {

// Where each thread starts (pthread_create's start routine).
thread_entry :: (arg: *void) -> *void #c_call {
    thread := cast(*Thread) arg;
    push_context thread.starting_context {
        thread.result = thread.proc(thread);
    }
    return null;
}

Timespec :: struct {
    seconds: s64;
    nanoseconds: s64;
}
CLOCK_REALTIME :: 0;

deadline_after :: (milliseconds: s32) -> Timespec {
    deadline: Timespec;
    clock_gettime(CLOCK_REALTIME, *deadline);
    deadline.nanoseconds += cast(s64) milliseconds % 1000 * 1_000_000;
    deadline.seconds += cast(s64) milliseconds / 1000 + deadline.nanoseconds / 1_000_000_000;
    deadline.nanoseconds %= 1_000_000_000;
    return deadline;
}

libc :: #system_library "libc";

__ojay_atomic_cas64 :: (p: *s64, expected: s64, desired: s64) -> s64 #foreign libc;  // (the runtime)

pthread_create :: (thread: *u64, attr: *void, start: (*void) -> *void #c_call, arg: *void) -> s32 #foreign libc;
pthread_join :: (thread: u64, result: **void) -> s32 #foreign libc;
pthread_tryjoin_np :: (thread: u64, result: **void) -> s32 #foreign libc;
pthread_timedjoin_np :: (thread: u64, result: **void, deadline: *Timespec) -> s32 #foreign libc;
pthread_mutex_init :: (m: *Mutex, attr: *void) -> s32 #foreign libc;
pthread_mutex_destroy :: (m: *Mutex) -> s32 #foreign libc;
pthread_mutex_lock :: (m: *Mutex) -> s32 #foreign libc;
pthread_mutex_unlock :: (m: *Mutex) -> s32 #foreign libc;
sem_init :: (s: *Semaphore, shared: s32, value: u32) -> s32 #foreign libc;
sem_destroy :: (s: *Semaphore) -> s32 #foreign libc;
sem_post :: (s: *Semaphore) -> s32 #foreign libc;
sem_wait :: (s: *Semaphore) -> s32 #foreign libc;
sem_timedwait :: (s: *Semaphore, deadline: *Timespec) -> s32 #foreign libc;
sched_yield :: () -> s32 #foreign libc;
clock_gettime :: (clock: s32, t: *Timespec) -> s32 #foreign libc;

} else {

// The runtime's part (runtime/ojay_runtime.c): stack switching and atomics.
runtime :: #system_library "libc";  // (resolved against the runtime library at link time)

__ojay_task_switch :: (save_sp: **void, new_sp: *void, new_limit: *void) #foreign runtime;
__ojay_task_stack_init :: (stack_top: *u8, entry: (*void) #c_call, arg: *void) -> *void #foreign runtime;
__ojay_atomic_add64 :: (p: *s64, delta: s64) -> s64 #foreign runtime;
__ojay_atomic_swap64 :: (p: *s64, value: s64) -> s64 #foreign runtime;
__ojay_atomic_cas64 :: (p: *s64, expected: s64, desired: s64) -> s64 #foreign runtime;

// The lowest STACK_GUARD bytes of each thread's stack hold STACK_CANARY; the
// stack may not reach them (the Cortex-M33 faults first: its stack limit
// register is set just above them).
STACK_CANARY :: 0xC0DE57AC;
STACK_GUARD :: 256;

stack_limit :: (thread: *Thread) -> *void #c_call {
    if thread == *main_thread  return null;  // (the runtime knows the main stack's)
    return thread.stack + STACK_GUARD;
}

main_thread: Thread;   // the program's own thread (index 0) once threads run
current: *Thread;      // the running thread (null until the first thread_start)
events: s64;           // counts changes that may end a wait (starts, ends, unlocks, signals)

start_scheduler :: () {
    if current  return;
    main_thread.started = true;
    main_thread.next = *main_thread;
    current = *main_thread;
}

// Where each thread starts (on its own stack, from the runtime's trampoline).
// It never returns: when proc is done the thread leaves the ring for good.
thread_entry :: (arg: *void) #c_call {
    thread := cast(*Thread) arg;
    push_context thread.starting_context {
        thread.result = thread.proc(thread);
        check_stack(thread);
    }
    thread.done = true;
    events += 1;
    before := thread;
    while before.next != thread  before = before.next;
    before.next = thread.next;
    current = thread.next;
    __ojay_task_switch(*thread.saved_sp, current.saved_sp, stack_limit(current));
}

// Stops the program if the thread wrote into the guard at the bottom of its
// stack (it overflowed, or nearly).
check_stack :: (thread: *Thread) {
    guard := cast(*u32) thread.stack;
    for i: 0..STACK_GUARD / 4 - 1  if guard[i] != STACK_CANARY {
        write_string("Thread: a thread overflowed its stack (give it a larger stack_size)\n", true);
        exit(1);
    }
}

thread_finished :: (thread: *void) -> bool { return (cast(*Thread) thread).done; }
mutex_free :: (m: *void) -> bool { return !(cast(*Mutex) m).locked; }
semaphore_signaled :: (s: *void) -> bool { return (cast(*Semaphore) s).count > 0; }

// Yields until ready(data). milliseconds == 0: lets the others run once and
// then says whether it is ready. Otherwise it waits; when a whole round of the
// other threads changed nothing and they are all waiting too, nothing can
// make it ready: that ends a wait with a timeout (false) and is a deadlock
// without one.
wait_until :: (ready: (data: *void) -> bool, data: *void, milliseconds: s32, what: string) -> bool {
    if ready(data)  return true;
    if milliseconds == 0 {
        thread_yield();
        return ready(data);
    }
    me := current;
    if me  me.waiting = true;
    while true {
        seen := events;
        thread_yield();
        if ready(data)  break;
        if events == seen && !others_running(me) {
            if me  me.waiting = false;
            if milliseconds > 0  return false;
            write_string(what, true);
            write_string(": deadlock (every thread is waiting)\n", true);
            exit(1);
        }
    }
    if me  me.waiting = false;
    events += 1;
    return true;
}

others_running :: (me: *Thread) -> bool {
    if !me  return false;
    t := me.next;
    while t != me {
        if !t.waiting  return true;
        t = t.next;
    }
    return false;
}

}
