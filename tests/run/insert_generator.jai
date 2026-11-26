#import "Basic";

unrolled_power :: (x: int, $N: int) -> int {
    result := 1;
    #insert -> string {
        s := "";
        for 1..N  s = join(s, "result *= x;\n");
        return s;
    }
    return result;
}

Fields :: struct {
    #insert -> string {
        s := "";
        for i: 0..2  s = join(s, tprint("f%: int = %;\n", i, i * 10));
        return s;
    }
}

main :: () {
    print("% %\n", unrolled_power(2, 8), unrolled_power(3, 3));
    f: Fields;
    print("%\n", f);
    x := #insert -> string { return "40 + 2"; };
    print("%\n", x);
    #insert -> Code { return #code print("from code\n"); };
}
