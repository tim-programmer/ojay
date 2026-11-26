// Constants inside structs: S.N, value.N, constants in member types and
// defaults, type and procedure constants, polymorphic structs, #insert, and
// their entries in Type_Info (flag CONSTANT).
// COMPILE_OUTPUT: run: N 3 count 3 size 32 BUF 8 T float32 make 5
// COMPILE_OUTPUT: run: members N(C) data(@0) T(C) make(C) scale(@24) LIMIT(C)
#import "Basic";

S :: struct {
    N :: 3;
    data: [N] int;
    T :: float32;
    make :: (x: int) -> S {
        s: S;
        for 0..N-1  s.data[it] = x + it;
        return s;
    }
    scale: T = 1.5;
    LIMIT : s16 : 100;
}

Buf :: struct ($N: int) {
    SIZE :: N * 2;
    items: [SIZE] u8;
}

Node :: struct {
    KIND :: "node";
    value: int;
}

// Constants made by #insert, a string and a #code block.
Inserted :: struct {
    #insert "WIDTH :: 4; HEIGHT :: WIDTH * 2;";
    #insert #code { AREA :: WIDTH * HEIGHT; cells: [AREA] u8; };
}

flag_names :: (f: Type_Info_Struct_Member_Flags) -> string {
    if f & .CONSTANT  return "C";
    return "";
}

describe :: (info: *Type_Info_Struct) -> string {
    b: String_Builder;
    init_string_builder(*b);
    append(*b, "members");
    for info.members {
        if it.flags & .CONSTANT  print_to_builder(*b, " %(C)", it.name);
        else                     print_to_builder(*b, " %(@%)", it.name, it.offset_in_bytes);
    }
    return builder_to_string(*b);
}

summary :: () -> string {
    s := S.make(5);
    b := Buf(4).SIZE;
    return sprint("N % count % size % BUF % T % make %", S.N, s.data.count, size_of(S), b, S.T, s.data[0]);
}

#run print("run: %\n", summary());
#run print("run: %\n", describe(type_info(S)));

main :: () {
    print("S.N = %, size_of(S) = %\n", S.N, size_of(S));
    s := S.make(10);
    print("s = %, s.N = %, s.scale = %\n", s, s.N, s.scale);
    p := *s;
    print("p.N = %, p.LIMIT = %, type_of(S.LIMIT) = %\n", p.N, p.LIMIT, type_of(S.LIMIT));
    x: S.T = 2.25;
    print("S.T: % (%)\n", x, type_of(x));

    b: Buf(4);
    print("Buf(4).SIZE = %, b.SIZE = %, items.count = %, Buf(10).SIZE = %\n", Buf(4).SIZE, b.SIZE, b.items.count,
          Buf(10).SIZE);

    n := Node.{value = 7};
    print("%: %\n", Node.KIND, n.value);

    print("Inserted: % % % size %\n", Inserted.WIDTH, Inserted.HEIGHT, Inserted.AREA, size_of(Inserted));

    // Constants are listed in Type_Info members, with offset 0.
    print("%\n", describe(type_info(S)));
    info := type_info(S);
    for info.members  if it.flags & .CONSTANT  print("  % : %\n", it.name, cast(Type) it.type);
    print("Node: %\n", describe(type_info(Node)));
    print("summary: %\n", summary());
}
