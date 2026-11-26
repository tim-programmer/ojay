#import "Basic";

power :: (x: int, $$n: int) -> int, string {
    result := 1;
    for 1..n  result *= x;
    #if is_constant(n)  return result, tprint("n = % baked", n);
    else                return result, "n at run time";
}

describe :: ($$v: $T) -> string {
    #if is_constant(v)  return tprint("constant % (%)", v, T);
    else                return tprint("variable % (%)", v, T);
}

with_default :: (x: int, $$scale := 10) -> int {
    #if is_constant(scale)  return x * scale + 1;
    else                    return x * scale;
}

main :: () {
    a, how := power(2, 10);
    print("% %\n", a, how);
    k := 5;
    a, how = power(3, k);
    print("% %\n", a, how);
    print("%\n", describe(7));
    print("%\n", describe(2.5));
    s := "hello";
    print("%\n", describe(s));
    print("%\n", describe("lit"));
    print("%\n", with_default(4));
    print("%\n", with_default(4, k));
    print("%\n", with_default(4, 2));
}
