// The context, push_context and custom allocators (also under #run).
// COMPILE_OUTPUT: #run: bump used 216 of 1024, counting allocs=2 resizes=1 frees=2
// COMPILE_OUTPUT: #run: restored=true temp=compile time 7
#import "Basic";

// ---- allocators written in Jai

Counter :: struct {
    allocs: int;
    resizes: int;
    frees: int;
    bytes: int;
}

// Counts what it is asked for and passes it on to the default allocator.
counting_allocator :: (mode: Allocator_Mode, size: s64, old_size: s64, old_memory: *void, allocator_data: *void) -> *void {
    c := cast(*Counter) allocator_data;
    if mode == {
        case .ALLOCATE; c.allocs += 1; c.bytes += size;
        case .RESIZE;   c.resizes += 1;
        case .FREE;     if old_memory  c.frees += 1;
    }
    return default_allocator_proc(mode, size, old_size, old_memory, null);
}

// Hands out memory from a fixed buffer; FREE does nothing, FREE_ALL starts over.
Bump :: struct {
    buffer: [1024] u8;
    used: int;
}

bump_allocator :: (mode: Allocator_Mode, size: s64, old_size: s64, old_memory: *void, allocator_data: *void) -> *void {
    b := cast(*Bump) allocator_data;
    if mode == {
        case .ALLOCATE; #through;
        case .RESIZE;
            n := (size + 7) / 8 * 8;
            if b.used + n > b.buffer.count  return null;
            result := *b.buffer[b.used];
            b.used += n;
            if mode == .RESIZE && old_memory  memcpy(result, old_memory, min(old_size, size));
            return result;
        case .FREE_ALL;
            b.used = 0;
    }
    return null;
}

counting :: (c: *Counter) -> Allocator { return .{counting_allocator, c}; }
bump :: (b: *Bump) -> Allocator { return .{bump_allocator, b}; }

// ---- context propagation

// Which allocator does the caller's context have?
allocator_name :: () -> string {
    if context.allocator.proc == counting_allocator  return "counting";
    if context.allocator.proc == bump_allocator  return "bump";
    if context.allocator.proc == default_allocator_proc  return "default";
    return "other";
}

nested :: (depth: int) -> string {
    if depth == 0  return allocator_name();
    return nested(depth - 1);
}

poly_name :: (x: $T) -> string { return allocator_name(); }

make_node :: () -> *Node { return New(Node); }  // allocates with the caller's context

Node :: struct {
    value: int = 7;
    next: *Node;
}

// ---- push_context restored on every exit

ctx_with :: (a: Allocator) -> Context {
    c := context;
    c.allocator = a;
    return c;
}

return_inside :: (c: *Counter) -> string {
    push_context ctx_with(counting(c)) {
        defer print("  defer in push_context sees %\n", allocator_name());
        return allocator_name();
    }
    return "unreachable";
}

loops :: (c: *Counter) {
    for i: 0..3 {
        push_context ctx_with(counting(c)) {
            if i == 1  continue;
            if i == 2  break;
            print("  iteration % uses %\n", i, allocator_name());
        }
    }
    print("  after the loop: %\n", allocator_name());
}

leave :: (c: *Counter) #expand {
    push_context ctx_with(counting(c)) {
        print("  in the macro: %\n", allocator_name());
        `return;
    }
}

macro_return :: (c: *Counter) {
    leave(c);
    print("not reached\n");
}

// Sets the context's allocator directly: as in Jai this changes the context
// it was given, i.e. its caller's (here: the pushed one, which is discarded).
set_directly :: (c: *Counter) {
    context.allocator = counting(c);
}

// ---- #add_context and the logger

#add_context frame_count: int = 3;

Log_Buffer :: struct {
    lines: int;
    last: string;
}

buffer_logger :: (message: string, data: *void, info: Log_Info) {
    b := cast(*Log_Buffer) data;
    b.lines += 1;
    b.last = message;
}

// ---- a #c_call callback that gives itself a context

saved_context: Context;

callback :: (x: s32) -> s32 #c_call {
    push_context saved_context {
        p := New(s32);
        p.* = x * 2;
        r := p.*;
        free(p);
        return r;
    }
}

// ---- the same code at compile time

compile_time :: () -> int {
    b: Bump;
    c: Counter;
    push_context ctx_with(bump(*b)) {
        n := New(Node);
        values: [..] int;
        for 1..10 array_add(*values, it);
        s := sprint("% %", n.value, values[9]);
        assert(s == "7 10");
        assert(cast(*u8) n >= b.buffer.data && cast(*u8) n < b.buffer.data + b.buffer.count);
    }
    push_allocator(counting(*c));
    p := New(int);
    s := sprint("%", 12);
    a: [..] int;
    array_add(*a, 1);
    free(p);
    free(s);
    print("#run: bump used % of %, counting allocs=% resizes=% frees=%\n", b.used, b.buffer.count, c.allocs,
          c.resizes, c.frees);
    return b.used;
}

quiet_loops :: (c: *Counter) -> bool {
    for i: 0..3 {
        push_context ctx_with(counting(c)) {
            if i == 1  continue;
            if i == 2  break;
        }
    }
    return context.allocator.proc != counting_allocator;
}

restored_at_compile_time :: () -> bool {
    c: Counter;
    before := context.allocator.proc;
    ok := quiet_loops(*c) && context.allocator.proc == before;
    t := tprint("compile time %", context.frame_count + 4);
    print("#run: restored=% temp=%\n", ok, t);
    reset_temporary_storage();
    return ok;
}

BUMP_USED :: #run compile_time();
RESTORED :: #run restored_at_compile_time();

main :: () {
    print("default: %, context.allocator is the default: %\n", allocator_name(),
          context.allocator.proc == default_allocator_proc);
    print("compile time: bump used %, restored %\n", BUMP_USED, RESTORED);

    // A counting allocator for New, array_add, sprint and free.
    c: Counter;
    push_context ctx_with(counting(*c)) {
        n := New(Node);
        s := sprint("node % at depth %", n.value, 2);
        values: [..] int;
        for 1..20  array_add(*values, it * it);
        print("% (% values, last %)\n", s, values.count, values[values.count - 1]);
        free(n);
        free(s);
        array_free(values);
    }
    print("counting: allocs=% resizes=% frees=% bytes=%\n", c.allocs, c.resizes, c.frees, c.bytes);

    // A bump allocator over a buffer.
    b: Bump;
    push_context ctx_with(bump(*b)) {
        list: *Node;
        for 1..5 {
            n := make_node();
            n.value = it;
            n.next = list;
            list = n;
        }
        total := 0;
        n := list;
        while n { total += n.value; n = n.next; }
        names := NewArray(3, string);
        names[0] = sprint("a%", 1);
        names[1] = sprint("b%", 2);
        names[2] = sprint("c%", 3);
        print("bump: total % names % used % bytes\n", total, names, b.used);
    }
    print("after push_context: %\n", allocator_name());

    // Explicit allocators.
    c2: Counter;
    p := alloc(100, counting(*c2));
    q := New(int, allocator = counting(*c2));
    r := NewArray(4, float, allocator = counting(*c2));
    p = realloc(p, 200, 100, counting(*c2));
    free(p, counting(*c2));
    free(q, allocator = counting(*c2));
    free(r.data, counting(*c2));
    print("explicit: allocs=% resizes=% frees=% bytes=%\n", c2.allocs, c2.resizes, c2.frees, c2.bytes);

    // Propagation through calls, polymorphic procedures and procedure values.
    c3: Counter;
    f: () -> string = allocator_name;
    push_context ctx_with(counting(*c3)) {
        print("propagation: call % nested % poly % %, procedure value %\n", allocator_name(), nested(3),
              poly_name(1), poly_name("x"), f());
    }

    // Restored after return, break/continue and a macro's `return.
    c4: Counter;
    print("return_inside: %\n", return_inside(*c4));
    print("after return: %\n", allocator_name());
    loops(*c4);
    macro_return(*c4);
    print("after the macro's `return: %\n", allocator_name());

    // Assigning context.allocator changes the current context: here the
    // pushed one, so it ends with the push_context.
    c5: Counter;
    push_context context {
        set_directly(*c5);
        print("after set_directly: %\n", allocator_name());
    }
    print("outside: %\n", allocator_name());

    // push_allocator: until the end of the scope.
    {
        push_allocator(counting(*c5));
        print("push_allocator: %\n", allocator_name());
    }
    print("after its scope: %\n", allocator_name());

    // A dynamic array keeps the allocator it first grew with.
    c6: Counter;
    arr: [..] int;
    push_context ctx_with(counting(*c6)) {
        array_add(*arr, 1);
    }
    for 2..40  array_add(*arr, it);  // grows with the counting allocator
    print("array: count % resizes % allocator is counting: %\n", arr.count, c6.resizes,
          arr.allocator.proc == counting_allocator);
    array_reset(*arr);
    print("array_reset: frees % allocator kept: %\n", c6.frees, arr.allocator.proc == counting_allocator);

    // Temporary storage.
    for 1..3 {
        t := tprint("temporary % of %", it, 3);
        print("%\n", t);
    }
    big := talloc(10000);  // bigger than a page
    memset(big, 1, 10000);
    ts := context.temporary_storage;
    print("temporary: occupied % high water mark >= 10000: %\n", ts.total_occupied, ts.high_water_mark >= 10000);
    reset_temporary_storage();
    print("after reset: occupied %\n", ts.total_occupied);
    push_context ctx_with(temporary_allocator) {
        s := sprint("in temporary storage: %", 99);
        print("%\n", s);
    }
    reset_temporary_storage();

    // #add_context and context fields.
    print("frame_count %\n", context.frame_count);
    child := context;
    child.frame_count = 10;
    child.user_index = 5;
    push_context child {
        context.frame_count += 1;
        print("in child: frame_count % user_index %\n", context.frame_count, context.user_index);
    }
    print("frame_count % (child's is %)\n", context.frame_count, child.frame_count);

    // The logger.
    lb: Log_Buffer;
    logged := context;
    logged.logger = buffer_logger;
    logged.logger_data = *lb;
    push_context logged {
        context.logger("hello", context.logger_data, .{});
        context.logger("world", context.logger_data, .{});
    }
    print("logger: % lines, last %\n", lb.lines, lb.last);
    context.logger("default logger\n", context.logger_data, .{});

    // A #c_call procedure, called through a procedure value.
    saved_context = context;
    cb: (s32) -> s32 #c_call = callback;
    print("callback: %\n", cb(21));
}
