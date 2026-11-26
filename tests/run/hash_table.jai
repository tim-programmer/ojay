#import "Basic";
#import "Hash_Table";

Color :: enum { RED; GREEN; BLUE; }

main :: () {
    t: Table(string, int);
    table_add(*t, "one", 1);
    table_add(*t, "two", 2);
    table_set(*t, "three", 3);
    table_set(*t, "one", 11);
    v, ok := table_find(*t, "one");
    print("% % count %\n", v, ok, t.count);
    v, ok = table_find(*t, "four");
    print("% %\n", v, ok);
    print("%\n", table_contains(*t, "two"));
    removed, old := table_remove(*t, "two");
    print("% % %\n", removed, old, table_contains(*t, "two"));

    squares: Table(int, int);
    for 0..999  table_add(*squares, it, it * it);
    print("% %\n", squares.count, table_find_pointer(*squares, 31).*);
    for 0..999  if it % 2  table_remove(*squares, it);
    total := 0;
    for squares  total += it;
    print("% %\n", squares.count, total);
    for * squares  it.* += 1;
    print("%\n", table_find_pointer(*squares, 10).*);
    for squares  if it_index >= 10  remove it;
    print("%\n", squares.count);

    by_color: Table(Color, string);
    table_add(*by_color, .GREEN, "go");
    p, added := find_or_add(*by_color, .RED);
    p.* = "stop";
    print("% % %\n", added, table_find_pointer(*by_color, .RED).*, by_color.count);
    deinit(*squares);
    deinit(*t);
}
