// ojay's Preload: the context and allocators. Its declarations are visible in
// every module (after the module's own), but a program only contains what it
// uses; a program that never uses any of it calls the runtime directly.
// The compiler relies on: Allocator, Context (allocator first), __allocate,
// __default_context and temporary_allocator.

Allocator_Mode :: enum {
    ALLOCATE;
    RESIZE;
    FREE;
    FREE_ALL;
    // ojay extension: `size` bytes aligned to `old_size` (a power of two),
    // for New / NewArray / dynamic arrays of #align types. An allocator that
    // returns null for it (as for any mode it does not handle) is asked with
    // ALLOCATE instead, and that block's alignment is checked.
    ALLOCATE_ALIGNED;
}

Allocator_Proc :: #type (mode: Allocator_Mode, size: s64, old_size: s64, old_memory: *void, allocator_data: *void) -> *void;

Allocator :: struct {
    proc: Allocator_Proc;
    data: *void;
}

Log_Flags :: enum_flags u32 {
    NONE              :: 0;
    ERROR             :: 0x1;
    WARNING           :: 0x2;
    CONTENT           :: 0x4;
    VERBOSE_ONLY      :: 0x8;
    VERY_VERBOSE_ONLY :: 0x10;
}

Log_Info :: struct {
    source_identifier: string;
    common_flags: Log_Flags;
    user_flags: u32;
}

Logger :: #type (message: string, data: *void, info: Log_Info);

// Temporary storage: memory handed out by temporary_allocator (and tprint) in
// pages; reset_temporary_storage() frees it all at once.
Temporary_Storage_Page :: struct {
    next: *Temporary_Storage_Page;  // the page filled before this one
    size: s64;                      // bytes of memory following this header
}

Temporary_Storage :: struct {
    page: *Temporary_Storage_Page;  // the page being filled (null before the first allocation)
    occupied: s64;                  // bytes used in `page`
    total_occupied: s64;            // bytes in use in all pages
    high_water_mark: s64;           // the most bytes ever in use at once
    last: *u8;                      // the latest allocation (RESIZE grows it in place)
    page_size: s64 = 4096;          // size of new pages (larger requests get a page of their own)
}

Context :: struct {
    allocator: Allocator;
    logger: Logger;
    logger_data: *void;
    log_source_identifier: string;
    temporary_storage: *Temporary_Storage;
    thread_index: u32;
    user_index: u32;
    user_data: *void;
    // #add_context members follow.
}

// The runtime's heap (malloc on hosted Linux, the runtime's own allocator on bare metal).
default_allocator_proc :: (mode: Allocator_Mode, size: s64, old_size: s64, old_memory: *void, allocator_data: *void) -> *void {
    if mode == {
        case .ALLOCATE; return __ojay_raw_alloc(size);
        case .ALLOCATE_ALIGNED; return __ojay_raw_alloc_aligned(size, old_size);
        case .RESIZE;   return __ojay_raw_realloc(old_memory, size, old_size);
        case .FREE;     __ojay_raw_free(old_memory);
    }
    return null;
}

temporary_allocator_proc :: (mode: Allocator_Mode, size: s64, old_size: s64, old_memory: *void, allocator_data: *void) -> *void {
    ts := context.temporary_storage;
    if !ts  ts = *__default_temporary_storage;
    if mode == {
        case .ALLOCATE;
            return __temporary_alloc(ts, size);
        case .ALLOCATE_ALIGNED;
            // Room for the alignment, then the first aligned address in it.
            p := cast(*u8) __temporary_alloc(ts, size + old_size - 1);
            if !p  return null;
            return cast(*void) ((cast(u64) p + cast(u64) (old_size - 1)) & ~cast(u64) (old_size - 1));
        case .RESIZE;
            if old_memory && ts.page && cast(*u8) old_memory == ts.last {
                // The latest allocation grows (or shrinks) in place when the page has room.
                used := ts.last - (cast(*u8) ts.page + size_of(Temporary_Storage_Page));
                n := __temporary_round(size);
                if size >= 0 && used + n <= ts.page.size {
                    __temporary_occupy(ts, used + n - ts.occupied);
                    return old_memory;
                }
            }
            result := __temporary_alloc(ts, size);
            if result && old_memory  memcpy(result, old_memory, min(old_size, size));
            return result;
        case .FREE_ALL;
            __reset_temporary_storage(ts);
    }
    return null;  // FREE: temporary memory is only freed all at once
}

temporary_allocator: Allocator = .{temporary_allocator_proc, null};

// Frees everything allocated with temporary_allocator (and tprint) so far.
reset_temporary_storage :: () {
    ts := context.temporary_storage;
    if !ts  ts = *__default_temporary_storage;
    __reset_temporary_storage(ts);
}

talloc :: (size: s64) -> *void {
    return temporary_allocator_proc(.ALLOCATE, size, 0, null, null);
}

// Makes `a` context.allocator until the end of the calling scope.
push_allocator :: (a: Allocator) #expand {
    __old_allocator := context.allocator;
    context.allocator = a;
    `defer context.allocator = __old_allocator;
}

__temporary_round :: (size: s64) -> s64 {
    return (size + 7) / 8 * 8;
}

__temporary_occupy :: (ts: *Temporary_Storage, n: s64) {
    ts.occupied += n;
    ts.total_occupied += n;
    if ts.total_occupied > ts.high_water_mark  ts.high_water_mark = ts.total_occupied;
}

__temporary_alloc :: (ts: *Temporary_Storage, size: s64) -> *void {
    if size < 0 return null;
    n := __temporary_round(size);
    if !ts.page || ts.occupied + n > ts.page.size {
        want := ts.page_size;
        if want < n  want = n;
        page := cast(*Temporary_Storage_Page) __ojay_raw_alloc(size_of(Temporary_Storage_Page) + want);
        page.next = ts.page;
        page.size = want;
        ts.page = page;
        ts.occupied = 0;
    }
    result := cast(*u8) ts.page + size_of(Temporary_Storage_Page) + ts.occupied;
    __temporary_occupy(ts, n);
    ts.last = result;
    return result;
}

__reset_temporary_storage :: (ts: *Temporary_Storage) {
    // Keeps the first page.
    while ts.page && ts.page.next {
        next := ts.page.next;
        __ojay_raw_free(ts.page);
        ts.page = next;
    }
    ts.occupied = 0;
    ts.total_occupied = 0;
    ts.last = null;
}

__default_logger :: (message: string, data: *void, info: Log_Info) {
    if message.count == 0  return;
    to_stderr := (cast(u32) info.common_flags & cast(u32) Log_Flags.ERROR) != 0;
    write_string(message, to_stderr);
    if message[message.count - 1] != #char "\n"  write_string("\n", to_stderr);
}

// An allocation through allocator `a`, or through context.allocator when `a`
// (or its proc) is null. The allocating built-ins (alloc, free, New,
// array_add, sprint, ...) call this.
__allocate :: (mode: Allocator_Mode, size: s64, old_size: s64, old_memory: *void, a: *Allocator) -> *void {
    allocator := a;
    if !allocator || !allocator.proc  allocator = *context.allocator;
    if allocator.proc == default_allocator_proc  return default_allocator_proc(mode, size, old_size, old_memory, null);
    return allocator.proc(mode, size, old_size, old_memory, allocator.data);
}

// New / NewArray / dynamic arrays of a type aligned more than the heap
// guarantees (#align members): the allocator is asked with ALLOCATE_ALIGNED
// (the runtime's heap gives an aligned block, which free accepts as usual).
// An allocator that does not handle it is asked with ALLOCATE, and what it
// returns is checked.
__allocate_aligned :: (size: s64, alignment: s64, a: *Allocator) -> *void {
    allocator := a;
    if !allocator || !allocator.proc  allocator = *context.allocator;
    if allocator.proc == default_allocator_proc  return __ojay_raw_alloc_aligned(size, alignment);
    p := allocator.proc(.ALLOCATE_ALIGNED, size, alignment, null, allocator.data);
    if !p  p = allocator.proc(.ALLOCATE, size, 0, null, allocator.data);
    if (cast(u64) p) & cast(u64) (alignment - 1) {
        write_string("New: the allocator returned memory that is not aligned as the type needs (#align)\n", true);
        exit(1);
    }
    return p;
}

__default_temporary_storage: Temporary_Storage;

__default_context: Context = .{
    allocator = .{default_allocator_proc, null},
    logger = __default_logger,
    temporary_storage = *__default_temporary_storage,
};
