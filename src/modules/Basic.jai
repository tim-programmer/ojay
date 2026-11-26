// The part of module Basic that is written in Jai. The rest (print, sprint,
// alloc, New, array_add, array_reserve, ...) is built into the compiler.
// Like the Preload, these declarations are only checked and compiled when a
// program uses them.

// ------------------------------------------------------------------ arrays

// Makes `array` hold `new_count` items. New items get T's default value
// unless `initialize` is false; a shorter count keeps the memory.
array_resize :: (array: *[..] $T, new_count: s64, initialize := true) {
    if new_count > array.allocated  array_reserve(array, new_count);
    if initialize && new_count > array.count {
        blank: T;
        for i: array.count..new_count-1  array.data[i] = blank;
    }
    array.count = new_count;
}

// Replaces the items of `dest` with copies of those of `source`.
array_copy :: (dest: *[..] $T, source: [] T) {
    if source.count > dest.allocated  array_reserve(dest, source.count);
    if source.count  memcpy(dest.data, source.data, source.count * size_of(T));
    dest.count = source.count;
}

// A new array (allocated with context.allocator) holding copies of `array`'s items.
array_copy :: (array: [] $T) -> [] T {
    result := NewArray(array.count, T, initialized = false);
    if array.count  memcpy(result.data, array.data, array.count * size_of(T));
    return result;
}

// Whether `item` is in `array`, and the index of its first occurrence (-1 if none).
array_find :: (array: [] $T, item: T) -> bool, s64 {
    for array  if it == item  return true, it_index;
    return false, -1;
}

// Adds `item` unless it is in `array` already. Returns whether it was added,
// and its index.
array_add_if_unique :: (array: *[..] $T, item: T) -> bool, s64 {
    found, index := array_find(array.*, item);
    if found  return false, index;
    array_add(array, item);
    return true, array.count - 1;
}

// Inserts `item` at `index` (0..count), moving the items from there up by one.
array_insert_at :: (array: *[..] $T, item: T, index: s64) {
    assert(index >= 0 && index <= array.count, "array_insert_at: index out of range");
    array_add(array);
    i := array.count - 1;
    while i > index {
        array.data[i] = array.data[i - 1];
        i -= 1;
    }
    array.data[index] = item;
}

// Removes the item at `index`, keeping the order of the others.
array_ordered_remove_by_index :: (array: *[..] $T, index: s64) {
    assert(index >= 0 && index < array.count, "array_ordered_remove_by_index: index out of range");
    for i: index..array.count-2  array.data[i] = array.data[i + 1];
    array.count -= 1;
}

// Removes the item at `index` by moving the last item there.
array_unordered_remove_by_index :: (array: *[..] $T, index: s64) {
    assert(index >= 0 && index < array.count, "array_unordered_remove_by_index: index out of range");
    last := array.count - 1;
    if index != last  array.data[index] = array.data[last];
    array.count -= 1;
}

// Removes every occurrence of `item`, keeping the order of the others.
// Returns how many were removed.
array_ordered_remove_by_value :: (array: *[..] $T, item: T) -> s64 {
    kept := 0;
    for i: 0..array.count-1 {
        if array.data[i] == item  continue;
        if kept != i  array.data[kept] = array.data[i];
        kept += 1;
    }
    removed := array.count - kept;
    array.count = kept;
    return removed;
}

// Removes occurrences of `item` (only the first one with stop_after_first),
// moving the last item into each hole. Returns how many were removed.
array_unordered_remove_by_value :: (array: *[..] $T, item: T, stop_after_first := false) -> s64 {
    removed := 0;
    i := 0;
    while i < array.count {
        if array.data[i] == item {
            array.data[i] = array.data[array.count - 1];
            array.count -= 1;
            removed += 1;
            if stop_after_first  break;
        } else {
            i += 1;
        }
    }
    return removed;
}

// `count` items of `array` from `index` on, without copying them.
array_view :: (array: [] $T, index: s64, count: s64) -> [] T {
    assert(index >= 0 && count >= 0 && index + count <= array.count, "array_view: range out of bounds");
    result: [] T;
    result.data = array.data + index;
    result.count = count;
    return result;
}

// The last item.
peek :: (array: [] $T) -> T {
    assert(array.count > 0, "peek: the array is empty");
    return array.data[array.count - 1];
}

// A pointer to the last item, or null if there is none.
peek_pointer :: (array: [] $T) -> *T {
    if array.count == 0  return null;
    return *array.data[array.count - 1];
}

// Removes the last item and returns it.
pop :: (array: *[..] $T) -> T {
    assert(array.count > 0, "pop: the array is empty");
    array.count -= 1;
    return array.data[array.count];
}

swap :: (a: *$T, b: *T) {
    t := a.*;
    a.* = b.*;
    b.* = t;
}

// ------------------------------------------------------------------ strings

// The length of a zero-terminated C string.
c_style_strlen :: (s: *u8) -> s64 {
    if !s  return 0;
    n := 0;
    while s[n]  n += 1;
    return n;
}

// A zero-terminated C string as a string (not copied).
to_string :: (s: *u8) -> string {
    result: string;
    result.data = s;
    result.count = c_style_strlen(s);
    return result;
}

// `count` bytes of memory as a string (not copied).
to_string :: (data: *u8, count: s64) -> string {
    result: string;
    result.data = data;
    result.count = count;
    return result;
}

// A new string of `count` bytes (allocated with context.allocator, not initialized).
alloc_string :: (count: s64) -> string {
    result: string;
    result.data = alloc(count);
    result.count = count;
    return result;
}

// A copy of `s` (allocated with context.allocator).
copy_string :: (s: string) -> string {
    if !s.count  return "";
    result := alloc_string(s.count);
    memcpy(result.data, s.data, s.count);
    return result;
}

// Compares bytewise: negative if a < b, 0 if equal, positive if a > b.
compare :: (a: string, b: string) -> s64 {
    n := ifx a.count < b.count then a.count else b.count;
    for i: 0..n-1 {
        if a[i] != b[i]  return cast(s64) a[i] - cast(s64) b[i];
    }
    return a.count - b.count;
}

is_digit :: (c: u8) -> bool { return c >= #char "0" && c <= #char "9"; }
is_alpha :: (c: u8) -> bool { return (c >= #char "a" && c <= #char "z") || (c >= #char "A" && c <= #char "Z"); }
is_alnum :: (c: u8) -> bool { return is_alpha(c) || is_digit(c) || c == #char "_"; }
is_space :: (c: u8) -> bool {
    return c == #char " " || c == #char "\t" || c == #char "\n" || c == #char "\r" || c == 11 || c == 12;
}

// ------------------------------------------------------------------ String_Builder

// Text assembled piece by piece. The memory comes from context.allocator
// when the builder first grows (it keeps that allocator).
String_Builder :: struct {
    buffer: [..] u8;
}

init_string_builder :: (builder: *String_Builder) {
    builder.* = .{};
}

append :: (builder: *String_Builder, s: string) {
    if !s.count  return;
    need := builder.buffer.count + s.count;
    if need > builder.buffer.allocated {
        cap := builder.buffer.allocated * 2;
        if cap < 64  cap = 64;
        if cap < need  cap = need;
        array_reserve(*builder.buffer, cap);
    }
    memcpy(builder.buffer.data + builder.buffer.count, s.data, s.count);
    builder.buffer.count = need;
}

append :: (builder: *String_Builder, byte: u8) {
    array_add(*builder.buffer, byte);
}

// Appends formatted text, as sprint formats it.
print_to_builder :: (builder: *String_Builder, format: string, args: .. Any) {
    s := sprint(format, ..args);
    append(builder, s);
    free(s);
}

builder_string_length :: (builder: *String_Builder) -> s64 {
    return builder.buffer.count;
}

// The text so far, as a new string (allocated with context.allocator).
// The builder is emptied (its memory is kept for reuse).
builder_to_string :: (builder: *String_Builder) -> string {
    result := alloc_string(builder.buffer.count);
    if result.count  memcpy(result.data, builder.buffer.data, result.count);
    builder.buffer.count = 0;
    return result;
}

// Empties the builder, keeping its memory.
reset :: (builder: *String_Builder) {
    builder.buffer.count = 0;
}

// Empties the builder and frees its memory.
free_buffers :: (builder: *String_Builder) {
    array_reset(*builder.buffer);
}

// ------------------------------------------------------------------ logging

// Formats a message (as sprint does) and hands it to context.logger.
log :: (format: string, args: .. Any) {
    __log(format, args, .NONE);
}

// log with Log_Flags.ERROR: the default logger writes it to stderr.
log_error :: (format: string, args: .. Any) {
    __log(format, args, .ERROR);
}

__log :: (format: string, args: [] Any, flags: Log_Flags) {
    message := sprint(format, ..args);
    info: Log_Info;
    info.source_identifier = context.log_source_identifier;
    info.common_flags = flags;
    if context.logger  context.logger(message, context.logger_data, info);
    free(message);
}
