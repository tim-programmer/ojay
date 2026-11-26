// Distinct types (#type,distinct): their own type with the base type's
// layout. No implicit conversions either way; casts and xx work; operators of
// the base type apply to two values of the distinct type (and to untyped
// constants); overloading, polymorphism and operator overloads see them as
// types of their own; Type_Info_Variant gives the name; across the C ABI they
// are their base type.
// COMPILE_OUTPUT: run: Handle 8 VARIANT u32 Handle Meters 3.5 Point {1, 2} Code_A 1
#import "Basic";

Handle :: #type,distinct u32;
Other  :: #type,distinct u32;
Meters :: #type,distinct float64;
Vec2   :: struct { x, y: float32; }
Point  :: #type,distinct Vec2;
Color  :: enum u8 { RED; GREEN; BLUE; }
Code   :: #type,distinct Color;
Row    :: #type,distinct [3] s32;
IntPtr :: #type,distinct *int;
Name   :: #type,distinct string;
Alias  :: Handle;    // an alias of the distinct type, not a new one

Pair :: struct { a: s32; d: float64; }
Pair_D :: #type,distinct Pair;
Doubler :: #type (x: s32) -> s32 #c_call;

next_handle :: (h: Handle) -> Handle #foreign;
swap_point  :: (p: Point) -> Point #foreign;
pair_sum    :: (p: Pair_D) -> float64 #foreign;
apply_twice :: (f: Doubler, x: s32) -> s32 #foreign;
double_it   :: (x: s32) -> s32 #c_call { return x * 2; }

describe :: (x: u32) -> string { return "u32"; }
describe :: (x: Handle) -> string { return "Handle"; }
describe :: (x: Meters) -> string { return "Meters"; }

kind_of :: (x: $T) -> Type { return T; }

operator + :: (a: Point, b: Point) -> Point { return cast(Point) Vec2.{a.x + b.x, a.y + b.y}; }
operator * :: (a: Meters, k: float64) -> Meters { return cast(Meters) (cast(float64) a * k * 10); }

variant_info :: (t: Type) -> string {
    info := cast(*Type_Info) t;
    if info.type != .VARIANT  return "not a variant";
    v := cast(*Type_Info_Variant) info;
    return sprint("% % VARIANT %", v.name, v.runtime_size * 2, cast(Type) v.variant_of);
}

summary :: () -> string {
    h: Handle = 5;
    m := cast(Meters) 1.5 + cast(Meters) 2;
    p := cast(Point) Vec2.{1, 2};
    c := cast(Code) Color.GREEN;
    return sprint("% % Meters % Point % Code_A %", variant_info(Handle), describe(h), m, p,
                  cast(u8) c);
}

#run print("run: %\n", summary());

main :: () {
    h: Handle = 7;
    raw: u32 = 40;
    h2 := cast(Handle) raw;   // casts both ways
    back := cast(u32) h2;
    print("h = %, h2 = %, back = %\n", h, h2, back);

    // Arithmetic: two Handles, or a Handle and a constant.
    sum := h + h2;
    inc := h + 1;
    print("h + h2 = %, h + 1 = %, h < h2 = %, h == 7 = %\n", sum, inc, h < h2, h == 7);
    print("types: % % %\n", type_of(sum), type_of(inc), type_of(h == h2));
    x: Handle = xx raw;   // xx casts to the target type
    y: u32 = xx h;
    print("xx: % %\n", x, y);

    // Overloading and polymorphic inference see a type of its own.
    print("describe: % % %\n", describe(h), describe(raw), describe(cast(Meters) 1));
    print("kind_of: % % %\n", kind_of(h), kind_of(raw), kind_of(cast(Other) 3));

    a: Alias = 3;
    print("Alias: % (%)\n", a, type_of(a));

    // Floats, with an operator overload.
    m: Meters = 2.5;
    print("m * 2 = % (%)\n", m * 2, type_of(m * 2));

    // Structs: members, literals through a cast, operator overloads.
    p := cast(Point) Vec2.{1, 2};
    q: Point;
    q.x = 10;
    q.y = 20;
    r := p + q;
    print("r = % (%), r.x = %, size %\n", r, type_of(r), r.x, size_of(Point));
    lit := Point.{3, 4};
    print("lit = % (%)\n", lit, type_of(lit));
    v := cast(Vec2) r;
    print("v = % (%)\n", v, type_of(v));

    // Enums, arrays, pointers, strings.
    c := cast(Code) Color.BLUE;
    print("c = % (%)\n", c, type_of(c));
    row: Row;
    row[1] = 5;
    arr := cast([3] s32) row;
    print("row = %, arr = %, row.count = %\n", row, arr, row.count);
    n := 42;
    ip := cast(IntPtr) *n;
    print("ip.* = %, (%)\n", ip.*, type_of(ip));
    name := cast(Name) "jai";
    print("name = %, count %\n", name, name.count);

    // Type_Info: a Type_Info_Variant with the name.
    print("%\n", variant_info(Handle));
    print("%\n", variant_info(Point));
    print("%\n", variant_info(u32));
    print("summary: %\n", summary());

    // The C ABI: as the base types.
    print("C: next_handle % (%)\n", next_handle(h), type_of(next_handle(h)));
    print("C: swap_point %\n", swap_point(p));
    print("C: pair_sum %\n", pair_sum(cast(Pair_D) Pair.{3, 0.5}));
    print("C: apply_twice %\n", apply_twice(double_it, 5));
}
