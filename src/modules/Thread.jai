// Module Thread: threads, mutexes and semaphores, after Jai's module of the
// same name. Linux only (POSIX threads, through the C library). Importing it
// makes the context per thread: each thread starts in a copy of the context
// thread_init was called in, with its own temporary storage and thread_index.

#import "Basic";

#assert OS == .LINUX, "the Thread module needs an operating system (Linux): it uses POSIX threads";

Thread_Proc :: #type (thread: *Thread) -> s64;

Thread :: struct {
    proc: Thread_Proc;
    data: *void;              // for the procedure
    index: u32;               // its context.thread_index
    result: s64;              // what proc returned (once it is done)

    starting_context: Context;
    temporary_storage: Temporary_Storage;
    handle: u64;              // pthread_t
    started: bool;
    joined: bool;
}

// Prepares `thread` to run `proc` (thread_start starts it). The thread's
// context is a copy of the current one, with its own temporary storage.
thread_init :: (thread: *Thread, proc: Thread_Proc, temporary_storage_size: s32 = 16384) -> bool {
    thread.proc = proc;
    thread.index = cast(u32) atomic_add(*next_thread_index, 1);
    thread.starting_context = context;
    thread.temporary_storage = .{};
    thread.temporary_storage.page_size = temporary_storage_size;
    thread.starting_context.temporary_storage = *thread.temporary_storage;
    thread.starting_context.thread_index = thread.index;
    thread.started = false;
    thread.joined = false;
    return true;
}

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
        deadline: Timespec;
        clock_gettime(CLOCK_REALTIME, *deadline);
        deadline.nanoseconds += cast(s64) milliseconds % 1000 * 1_000_000;
        deadline.seconds += cast(s64) milliseconds / 1000 + deadline.nanoseconds / 1_000_000_000;
        deadline.nanoseconds %= 1_000_000_000;
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
    deadline: Timespec;
    clock_gettime(CLOCK_REALTIME, *deadline);
    deadline.nanoseconds += cast(s64) milliseconds % 1000 * 1_000_000;
    deadline.seconds += cast(s64) milliseconds / 1000 + deadline.nanoseconds / 1_000_000_000;
    deadline.nanoseconds %= 1_000_000_000;
    return sem_timedwait(s, *deadline) == 0;
}

// ------------------------------------------------------------------ atomics

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

#scope_file

next_thread_index: s64 = 1;  // 0 is the main thread

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

libc :: #system_library "libc";

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
clock_gettime :: (clock: s32, t: *Timespec) -> s32 #foreign libc;
