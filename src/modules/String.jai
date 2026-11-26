// Module String: string procedures, after Jai's module of the same name.
// Unless a procedure says it allocates (with context.allocator), the strings
// it returns are views into its arguments.

#import "Basic";

// ------------------------------------------------------------------ comparing

equal :: (a: string, b: string) -> bool { return a == b; }

equal_nocase :: (a: string, b: string) -> bool {
    if a.count != b.count  return false;
    for 0..a.count-1  if to_lower(a[it]) != to_lower(b[it])  return false;
    return true;
}

// -1, 0 or 1, ignoring the case of ASCII letters.
compare_nocase :: (a: string, b: string) -> int {
    n := ifx a.count < b.count then a.count else b.count;
    for 0..n-1 {
        x, y := to_lower(a[it]), to_lower(b[it]);
        if x != y  return ifx x < y then -1 else 1;
    }
    if a.count == b.count  return 0;
    return ifx a.count < b.count then -1 else 1;
}

starts_with :: (s: string, prefix: string) -> bool {
    if prefix.count > s.count  return false;
    return slice(s, 0, prefix.count) == prefix;
}
starts_with :: (s: string, c: u8) -> bool { return s.count > 0 && s[0] == c; }
begins_with :: (s: string, prefix: string) -> bool { return starts_with(s, prefix); }

ends_with :: (s: string, suffix: string) -> bool {
    if suffix.count > s.count  return false;
    return slice(s, s.count - suffix.count, suffix.count) == suffix;
}
ends_with :: (s: string, c: u8) -> bool { return s.count > 0 && s[s.count - 1] == c; }

// ------------------------------------------------------------------ searching

// The index of the first occurrence (-1 if none).
find_index_from_left :: (s: string, substring: string, start_index := 0) -> s64 {
    if substring.count == 0  return ifx start_index <= s.count then start_index else -1;
    i := start_index;
    while i + substring.count <= s.count {
        if s[i] == substring[0] && slice(s, i, substring.count) == substring  return i;
        i += 1;
    }
    return -1;
}
find_index_from_left :: (s: string, c: u8, start_index := 0) -> s64 {
    for start_index..s.count-1  if s[it] == c  return it;
    return -1;
}

// The index of the last occurrence (-1 if none).
find_index_from_right :: (s: string, substring: string) -> s64 {
    if substring.count == 0  return s.count;
    i := s.count - substring.count;
    while i >= 0 {
        if slice(s, i, substring.count) == substring  return i;
        i -= 1;
    }
    return -1;
}
find_index_from_right :: (s: string, c: u8) -> s64 {
    for < 0..s.count-1  if s[it] == c  return it;
    return -1;
}

// The index of the first byte that is one of `chars` (-1 if none).
find_index_of_any_from_left :: (s: string, chars: string) -> s64 {
    for 0..s.count-1  if is_any(s[it], chars)  return it;
    return -1;
}
find_index_of_any_from_right :: (s: string, chars: string) -> s64 {
    for < 0..s.count-1  if is_any(s[it], chars)  return it;
    return -1;
}

contains :: (s: string, substring: string) -> bool { return find_index_from_left(s, substring) >= 0; }
contains :: (s: string, c: u8) -> bool { return find_index_from_left(s, c) >= 0; }

is_any :: (c: u8, chars: string) -> bool {
    for 0..chars.count-1  if chars[it] == c  return true;
    return false;
}

// How many (non-overlapping) times `substring` occurs.
count :: (s: string, substring: string) -> s64 {
    if substring.count == 0  return 0;
    n := 0;
    i := find_index_from_left(s, substring);
    while i >= 0 {
        n += 1;
        i = find_index_from_left(s, substring, i + substring.count);
    }
    return n;
}
count :: (s: string, c: u8) -> s64 {
    n := 0;
    for 0..s.count-1  if s[it] == c  n += 1;
    return n;
}

// ------------------------------------------------------------------ slicing

// `count` bytes from `index`, clamped to the string.
slice :: (s: string, index: s64, count: s64) -> string {
    if index < 0  index = 0;
    if index > s.count  index = s.count;
    if count > s.count - index  count = s.count - index;
    if count < 0  count = 0;
    result: string;
    result.data = s.data + index;
    result.count = count;
    return result;
}
// From `index` to the end.
slice :: (s: string, index: s64) -> string { return slice(s, index, s.count - index); }

// Moves the start of `s` forward by `amount` bytes.
advance :: (s: *string, amount: s64 = 1) {
    if amount > s.count  amount = s.count;
    s.data += amount;
    s.count -= amount;
}

DEFAULT_TRIM :: " \t\r\n";

trim_left :: (s: string, chars := DEFAULT_TRIM) -> string {
    i := 0;
    while i < s.count && is_any(s[i], chars)  i += 1;
    return slice(s, i);
}
trim_right :: (s: string, chars := DEFAULT_TRIM) -> string {
    n := s.count;
    while n > 0 && is_any(s[n - 1], chars)  n -= 1;
    return slice(s, 0, n);
}
trim :: (s: string, chars := DEFAULT_TRIM) -> string { return trim_right(trim_left(s, chars), chars); }

// The text before and after the first `separator` (found = false: s and "").
split_from_left :: (s: string, separator: string) -> found: bool, left: string, right: string {
    i := find_index_from_left(s, separator);
    if i < 0  return false, s, "";
    return true, slice(s, 0, i), slice(s, i + separator.count);
}
split_from_left :: (s: string, separator: u8) -> found: bool, left: string, right: string {
    i := find_index_from_left(s, separator);
    if i < 0  return false, s, "";
    return true, slice(s, 0, i), slice(s, i + 1);
}
split_from_right :: (s: string, separator: string) -> found: bool, left: string, right: string {
    i := find_index_from_right(s, separator);
    if i < 0  return false, s, "";
    return true, slice(s, 0, i), slice(s, i + separator.count);
}
split_from_right :: (s: string, separator: u8) -> found: bool, left: string, right: string {
    i := find_index_from_right(s, separator);
    if i < 0  return false, s, "";
    return true, slice(s, 0, i), slice(s, i + 1);
}

// The pieces between the separators (the array is allocated; the pieces are
// views into s). An empty separator splits into single bytes.
split :: (s: string, separator: string) -> [] string {
    pieces: [..] string;
    if separator.count == 0 {
        for 0..s.count-1  array_add(*pieces, slice(s, it, 1));
        return pieces;
    }
    rest := s;
    while true {
        found, left, right := split_from_left(rest, separator);
        array_add(*pieces, left);
        if !found  break;
        rest = right;
    }
    return pieces;
}
split :: (s: string, separator: u8) -> [] string {
    sep: [1] u8;
    sep[0] = separator;
    return split(s, to_string(sep.data, 1));
}

// ------------------------------------------------------------------ making strings (allocated)

// `s` with every `old` replaced by `new`.
replace :: (s: string, old: string, new: string) -> string {
    if old.count == 0  return copy_string(s);
    builder: String_Builder;
    rest := s;
    while true {
        found, left, right := split_from_left(rest, old);
        append(*builder, left);
        if !found  break;
        append(*builder, new);
        rest = right;
    }
    result := builder_to_string(*builder);
    free_buffers(*builder);
    return result;
}

to_upper :: (c: u8) -> u8 { return ifx c >= #char "a" && c <= #char "z" then c - 32 else c; }
to_lower :: (c: u8) -> u8 { return ifx c >= #char "A" && c <= #char "Z" then c + 32 else c; }

to_upper_in_place :: (s: string) { for 0..s.count-1  s[it] = to_upper(s[it]); }
to_lower_in_place :: (s: string) { for 0..s.count-1  s[it] = to_lower(s[it]); }

to_upper_copy :: (s: string) -> string { r := copy_string(s); to_upper_in_place(r); return r; }
to_lower_copy :: (s: string) -> string { r := copy_string(s); to_lower_in_place(r); return r; }

// ------------------------------------------------------------------ numbers

// The integer at the start of `s` (after spaces; an optional sign; digits in
// `base`, 2 to 36). Returns it, whether there were digits, and the rest.
string_to_int :: (s: string, base := 10) -> result: int, success: bool, remainder: string {
    t := trim_left(s);
    negative := false;
    if t.count && (t[0] == #char "-" || t[0] == #char "+") {
        negative = t[0] == #char "-";
        advance(*t);
    }
    value: u64;
    digits := 0;
    while t.count {
        c := t[0];
        d: int = -1;
        if c >= #char "0" && c <= #char "9"       d = c - #char "0";
        else if c >= #char "a" && c <= #char "z"  d = c - #char "a" + 10;
        else if c >= #char "A" && c <= #char "Z"  d = c - #char "A" + 10;
        if d < 0 || d >= base  break;
        value = value * cast(u64) base + cast(u64) d;
        digits += 1;
        advance(*t);
    }
    if !digits  return 0, false, s;
    result := cast,no_check(int) value;
    return ifx negative then -result else result, true, t;
}

// The decimal number at the start of `s` (`-1.5`, `2e3`, `.25`). Returns it,
// whether there was one, and the rest.
string_to_float :: (s: string) -> result: float64, success: bool, remainder: string {
    t := trim_left(s);
    negative := false;
    if t.count && (t[0] == #char "-" || t[0] == #char "+") {
        negative = t[0] == #char "-";
        advance(*t);
    }
    value: float64;
    digits := 0;
    while t.count && is_digit(t[0]) {
        value = value * 10 + cast(float64) (t[0] - #char "0");
        digits += 1;
        advance(*t);
    }
    if t.count && t[0] == #char "." {
        advance(*t);
        scale: float64 = 0.1;
        while t.count && is_digit(t[0]) {
            value += cast(float64) (t[0] - #char "0") * scale;
            scale *= 0.1;
            digits += 1;
            advance(*t);
        }
    }
    if !digits  return 0, false, s;
    if t.count >= 2 && (t[0] == #char "e" || t[0] == #char "E") {
        exponent, ok, rest := string_to_int(slice(t, 1));
        if ok {
            p: float64 = 1;
            for 1..(ifx exponent < 0 then -exponent else exponent)  p *= 10;
            value = ifx exponent < 0 then value / p else value * p;
            t = rest;
        }
    }
    return ifx negative then -value else value, true, t;
}

// ------------------------------------------------------------------ paths

// "dir/name.ext" -> "dir/"
path_strip_filename :: (path: string) -> string {
    i := find_index_from_right(path, #char "/");
    return slice(path, 0, i + 1);
}
// "dir/name.ext" -> "name.ext"
path_filename :: (path: string) -> string {
    i := find_index_from_right(path, #char "/");
    return slice(path, i + 1);
}
// "dir/name.ext" -> "ext" (and whether there is one)
path_extension :: (path: string) -> string, bool {
    name := path_filename(path);
    i := find_index_from_right(name, #char ".");
    if i <= 0  return "", false;
    return slice(name, i + 1), true;
}
// "dir/name.ext" -> "dir/name"
path_strip_extension :: (path: string) -> string {
    _, has := path_extension(path);
    if !has  return path;
    return slice(path, 0, find_index_from_right(path, #char "."));
}
