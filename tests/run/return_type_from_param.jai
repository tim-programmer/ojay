#import "Basic";
#import "Hash_Table";
Pair :: struct ($A: Type, $B: Type = int) { a: A; b: B; }
first :: (p: Pair) -> p.A { return p.a; }
any_value :: (t: *Table) -> t.Value_Type { for t.* return it; v: t.Value_Type; return v; }
main :: () {
    p: Pair(string, float);
    p.a = "first";
    print("%\n", first(p));
    t: Table(int, string);
    table_add(*t, 1, "one");
    print("%\n", any_value(*t));
}
