// Module Hash_Table: a hash table with open addressing and linear probing,
// after Jai's module of the same name. Keys can be integers, enums, bools,
// floats, pointers and strings (each has a get_hash overload here); the keys
// are compared with ==. Iterate with `for table`: `it` is the value
// (`for * table`: a pointer to it), `it_index` the key; `remove it` removes
// the entry.

#import "Basic";

Table :: struct ($Key_Type: Type, $Value_Type: Type, $LOAD_FACTOR_PERCENT: int = 70)
#modify {
    tag := (cast(*Type_Info) Key_Type).type;
    return tag == .INTEGER || tag == .ENUM || tag == .BOOL || tag == .FLOAT || tag == .POINTER || tag == .STRING,
           "keys must be integers, enums, bools, floats, pointers or strings";
}
{
    count:        int;  // entries in the table
    allocated:    int;  // slots (a power of two, or 0)
    slots_filled: int;  // slots in use, removed ones included

    allocator: Allocator;  // set by init (or the first add) to context.allocator unless set before

    entries: [] Entry;

    Entry :: struct {
        hash:  u32;  // NEVER_OCCUPIED_HASH, REMOVED_HASH, or the key's hash (FIRST_VALID_HASH or more)
        key:   Key_Type;
        value: Value_Type;
    }
}

NEVER_OCCUPIED_HASH :: 0;
REMOVED_HASH        :: 1;
FIRST_VALID_HASH    :: 2;

SIZE_MIN :: 32;

// Makes room for `slots_to_allocate` slots (rounded up to a power of two; 0: a small default).
init :: (table: *Table($K, $V), slots_to_allocate: s64 = 0) {
    if !table.allocator.proc  table.allocator = context.allocator;
    n := SIZE_MIN;
    while n < slots_to_allocate  n *= 2;
    resize(table, n);
}

// Frees the table's memory.
deinit :: (table: *Table($K, $V)) {
    if table.entries.data  free(table.entries.data, table.allocator);
    table.entries.data = null;
    table.entries.count = 0;
    table.allocated = 0;
    table.count = 0;
    table.slots_filled = 0;
}

// Removes every entry, keeping the memory.
table_reset :: (table: *Table($K, $V)) {
    for * table.entries  it.hash = NEVER_OCCUPIED_HASH;
    table.count = 0;
    table.slots_filled = 0;
}

// Adds an entry for `key` (which must not be in the table yet; use table_set
// to replace) and returns a pointer to its value.
table_add :: (table: *Table($K, $V), key: K, value: V) -> *V {
    ensure_space_for_one_more(table);
    hash := get_valid_hash(key);
    index := cast(s64) hash & (table.allocated - 1);
    while table.entries[index].hash >= FIRST_VALID_HASH  index = (index + 1) & (table.allocated - 1);
    entry := *table.entries[index];
    if entry.hash == NEVER_OCCUPIED_HASH  table.slots_filled += 1;
    entry.hash = hash;
    entry.key = key;
    entry.value = value;
    table.count += 1;
    return *entry.value;
}

// Sets the value of `key`, adding it if needed; returns a pointer to the value.
table_set :: (table: *Table($K, $V), key: K, value: V) -> *V {
    p := table_find_pointer(table, key);
    if p {
        p.* = value;
        return p;
    }
    return table_add(table, key, value);
}

// A pointer to the value of `key`, or null if it is not in the table.
table_find_pointer :: (table: *Table($K, $V), key: K) -> *V {
    index := find_index(table, key);
    if index < 0  return null;
    return *table.entries[index].value;
}

// The value of `key` and true, or V's default value and false.
table_find :: (table: *Table($K, $V), key: K) -> V, bool {
    p := table_find_pointer(table, key);
    if p  return p.*, true;
    nothing: V;
    return nothing, false;
}

table_contains :: (table: *Table($K, $V), key: K) -> bool {
    return find_index(table, key) >= 0;
}

// Removes the entry of `key`. Returns whether there was one, and its value.
table_remove :: (table: *Table($K, $V), key: K) -> bool, V {
    index := find_index(table, key);
    if index < 0 {
        nothing: V;
        return false, nothing;
    }
    entry := *table.entries[index];
    entry.hash = REMOVED_HASH;
    table.count -= 1;
    return true, entry.value;
}

// A pointer to the value of `key`, adding an entry with V's default value if
// it is not there; and whether it was added.
find_or_add :: (table: *Table($K, $V), key: K) -> entry: *V, newly_added: bool {
    p := table_find_pointer(table, key);
    if p  return p, false;
    blank: V;
    return table_add(table, key, blank), true;
}

// Makes room for `items` more entries without growing while they are added.
table_ensure_space :: (table: *Table($K, $V), items: s64) {
    needed := (table.slots_filled + items) * 100;
    if table.allocated && needed < table.allocated * table.LOAD_FACTOR_PERCENT  return;
    n := ifx table.allocated then table.allocated else SIZE_MIN;
    while n * table.LOAD_FACTOR_PERCENT <= needed  n *= 2;
    if !table.allocator.proc  table.allocator = context.allocator;
    resize(table, n);
}

// `for table`: `it` is the value (`for * table`: a pointer to it), `it_index`
// the key. `remove it` removes the entry. The order is that of the slots
// (`for <` makes no difference).
for_expansion :: (table: *Table($K, $V), body: Code, flags: For_Flags) #expand {
    for * __entry: table.entries {
        if __entry.hash < FIRST_VALID_HASH  continue;
        #if flags & .POINTER {
            `it := *__entry.value;
        } else {
            `it := __entry.value;
        }
        `it_index := __entry.key;
        #insert (remove = { __entry.hash = REMOVED_HASH; table.count -= 1; }) body;
    }
}

// ------------------------------------------------------------------ hashes

// 32-bit FNV-1a of `count` bytes at `data`.
fnv1a_hash :: (data: *u8, count: s64, h: u32 = 2166136261) -> u32 {
    for 0..count-1 {
        h ^= data[it];
        h *= 16777619;
    }
    return h;
}

get_hash :: (x: string) -> u32 { return fnv1a_hash(x.data, x.count); }
get_hash :: (x: s64) -> u32 { return mix64(cast,no_check(u64) x); }
get_hash :: (x: u64) -> u32 { return mix64(x); }
get_hash :: (x: s32) -> u32 { return mix64(cast,no_check(u64) cast(s64) x); }
get_hash :: (x: u32) -> u32 { return mix64(x); }
get_hash :: (x: s16) -> u32 { return mix64(cast,no_check(u64) cast(s64) x); }
get_hash :: (x: u16) -> u32 { return mix64(x); }
get_hash :: (x: s8) -> u32 { return mix64(cast,no_check(u64) cast(s64) x); }
get_hash :: (x: u8) -> u32 { return mix64(x); }
get_hash :: (x: bool) -> u32 { return ifx x then 1231 else 1237; }
get_hash :: (x: float64) -> u32 {
    if x == 0  x = 0;  // -0 == 0
    return mix64((cast(*u64) *x).*);
}
get_hash :: (x: float32) -> u32 { return get_hash(cast(float64) x); }
get_hash :: (x: *$T) -> u32 { return mix64(cast(u64) x); }
get_hash :: (x: $T) -> u32 #modify { return (cast(*Type_Info) T).type == .ENUM, "not an enum"; } {
    return mix64(cast,no_check(u64) cast(s64) x);
}

#scope_file

// Spreads the bits of x over 32 (the finalizer of MurmurHash3).
mix64 :: (x: u64) -> u32 {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccd;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53;
    x ^= x >> 33;
    return cast,trunc(u32) x;
}

// The key's hash, kept clear of the two values that mark free slots.
get_valid_hash :: (key: $K) -> u32 {
    h := get_hash(key);
    if h < FIRST_VALID_HASH  h += FIRST_VALID_HASH;
    return h;
}

find_index :: (table: *Table($K, $V), key: K) -> s64 {
    if !table.allocated  return -1;
    hash := get_valid_hash(key);
    index := cast(s64) hash & (table.allocated - 1);
    while true {
        entry := *table.entries[index];
        if entry.hash == NEVER_OCCUPIED_HASH  return -1;
        if entry.hash == hash && entry.key == key  return index;
        index = (index + 1) & (table.allocated - 1);
    }
    return -1;
}

ensure_space_for_one_more :: (table: *Table($K, $V)) {
    if !table.allocated  init(table);
    if (table.slots_filled + 1) * 100 >= table.allocated * table.LOAD_FACTOR_PERCENT {
        // Mostly removed slots: rehash in place (same size); else grow.
        n := ifx table.count * 2 < table.slots_filled then table.allocated else table.allocated * 2;
        resize(table, n);
    }
}

// Moves the entries into `n` new slots.
resize :: (table: *Table($K, $V), n: s64) {
    old := table.entries;
    table.entries = NewArray(n, table.Entry, allocator = table.allocator);
    table.allocated = n;
    table.count = 0;
    table.slots_filled = 0;
    for * old {
        if it.hash < FIRST_VALID_HASH  continue;
        index := cast(s64) it.hash & (n - 1);
        while table.entries[index].hash != NEVER_OCCUPIED_HASH  index = (index + 1) & (n - 1);
        table.entries[index] = it.*;
        table.count += 1;
        table.slots_filled += 1;
    }
    if old.data  free(old.data, table.allocator);
}
