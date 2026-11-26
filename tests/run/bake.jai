#import "Basic";

add :: (a: int, b: int) -> int { return a + b; }
add_ten :: #bake_arguments add(b = 10);

scale :: (x: float, factor := 2.0) -> float { return x * factor; }
triple :: #bake_arguments scale(factor = 3);

repeat :: (s: string, $N: int) -> string {
    result := "";
    for 1..N  result = join(result, s);
    return result;
}
repeat3 :: #bake_constants repeat(N = 3);

fill :: ($T: Type, value: T, $N: int) -> [N] T {
    result: [N] T;
    for * result  it.* = value;
    return result;
}
fill_four_floats :: #bake_constants fill(T = float, N = 4);

show :: (x: $T, label: string) { print("%: % (%)\n", label, x, T); }
show_answer :: #bake_arguments show(x = 42);  // binds T = int
show_any :: #bake_arguments show(label = "any");  // still polymorphic

pick :: (a: $T, b: T, $FIRST: bool) -> T {
    #if FIRST  return a;
    else       return b;
}
pick_first :: #bake_constants pick(FIRST = true);
pick_second_s8 :: #bake_constants pick(FIRST = false, T = s8);

main :: () {
    print("%\n", add_ten(5));
    print("%\n", triple(1.5));
    print("%\n", repeat3("ab"));
    print("%\n", fill_four_floats(0.5));
    show_answer("answer");
    show_any(1.25);
    show_any("text");
    print("% %\n", pick_first(1, 2), pick_first("x", "y"));
    v := pick_second_s8(1, 2);
    print("% %\n", v, type_of(v));
    f := add_ten;
    print("%\n", f(1));
}
