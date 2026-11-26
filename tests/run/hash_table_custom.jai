#import "Basic";
#import "Hash_Table";

Point :: struct { x, y: int; }
hash_point :: (p: Point) -> u32 { return get_hash(p.x * 31 + p.y); }
points_equal :: (a: Point, b: Point) -> bool { return a.x == b.x && a.y == b.y; }

// Strings compared without case.
lower :: (c: u8) -> u8 { return ifx c >= #char "A" && c <= #char "Z" then c + 32 else c; }
hash_nocase :: (s: string) -> u32 {
    h: u32 = 2166136261;
    for 0..s.count-1 { h ^= lower(s[it]); h *= 16777619; }
    return h;
}
equal_nocase :: (a: string, b: string) -> bool {
    if a.count != b.count  return false;
    for 0..a.count-1  if lower(a[it]) != lower(b[it])  return false;
    return true;
}

main :: () {
    grid: Table(Point, string, hash_point, points_equal);
    table_add(*grid, .{1, 2}, "a");
    table_add(*grid, .{2, 1}, "b");
    table_set(*grid, .{1, 2}, "c");
    v, found := table_find(*grid, .{1, 2});
    print("% % %\n", v, found, grid.count);
    print("%\n", table_contains(*grid, .{3, 3}));

    words: Table(string, int, hash_nocase, equal_nocase);
    for string.["Apple", "apple", "APPLE", "pear"] {
        p, added := find_or_add(*words, it);
        p.* += 1;
    }
    a, _ := table_find(*words, "aPPle");
    print("% %\n", a, words.count);
}
