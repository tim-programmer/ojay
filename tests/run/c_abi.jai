// Structs by value across the C boundary (C side: c_abi.c). Jai passes
// structs to #foreign procedures and gets structs back; C calls Jai #c_call
// procedures (as procedure values and by their exported symbol names) with
// struct arguments and results. Every value is printed here.

#import "Basic";

S1  :: struct { a: s8; }
S2  :: struct { a: s16; }
S3  :: struct { a, b, c: s8; }
S4  :: struct { a: s32; }
S8  :: struct { a, b: s32; }
S12 :: struct { a, b, c: s32; }
S16 :: struct { a, b, c, d: s32; }
S24 :: struct { a, b, c: s64; }
S64 :: struct { a: [16]s32; }
S72 :: struct { a: [18]s32; }
FI  :: struct { f: float32; i: s32; }
DD  :: struct { a, b: float64; }
F1  :: struct { a: float32; }
F2  :: struct { a, b: float32; }
F3  :: struct { a, b, c: float32; }
F4  :: struct { a, b, c, d: float32; }
F5  :: struct { a, b, c, d, e: float32; }
D4  :: struct { a, b, c, d: float64; }
ID  :: struct { a: s8; d: float64; }
Color :: enum u8 { RED; GREEN; BLUE; }
Mixed :: struct { flag: bool; color: Color; small: u16; p: *s32; }
Inner :: struct { x, y: s16; }
Nested :: struct { name: [3]u8; inner: Inner; pts: [2]Inner; f: [2]float32; }
UFI :: union { f: float32; i: s32; }
Empty :: struct {}

// C returns each member + 1 (floats + 0.5; the bool negated, the next color,
// the pointer unchanged after incrementing its target).
bump_s1  :: (x: S1) -> S1 #foreign;
bump_s2  :: (x: S2) -> S2 #foreign;
bump_s3  :: (x: S3) -> S3 #foreign;
bump_s4  :: (x: S4) -> S4 #foreign;
bump_s8  :: (x: S8) -> S8 #foreign;
bump_s12 :: (x: S12) -> S12 #foreign;
bump_s16 :: (x: S16) -> S16 #foreign;
bump_s24 :: (x: S24) -> S24 #foreign;
bump_s64 :: (x: S64) -> S64 #foreign;
bump_s72 :: (x: S72) -> S72 #foreign;
bump_fi  :: (x: FI) -> FI #foreign;
bump_dd  :: (x: DD) -> DD #foreign;
bump_f1  :: (x: F1) -> F1 #foreign;
bump_f2  :: (x: F2) -> F2 #foreign;
bump_f3  :: (x: F3) -> F3 #foreign;
bump_f4  :: (x: F4) -> F4 #foreign;
bump_f5  :: (x: F5) -> F5 #foreign;
bump_d4  :: (x: D4) -> D4 #foreign;
bump_id  :: (x: ID) -> ID #foreign;
bump_mixed  :: (x: Mixed) -> Mixed #foreign;
bump_nested :: (x: Nested) -> Nested #foreign;
bump_ufi :: (x: UFI) -> UFI #foreign;

// Structs after enough scalars that they go (partly) to the stack.
spill_s16 :: (a: s32, b: s32, c: s32, d: s32, e: s32, f: s32, x: S16, g: s32) -> s64 #foreign;
spill_s8x2 :: (a: s32, b: s32, c: s32, d: s32, e: s32, f: s32, x: S8, y: S8) -> s64 #foreign;
split_s12 :: (a: s32, b: s32, c: s32, x: S12, y: S3) -> s64 #foreign;
spill_dd :: (a: float64, b: float64, c: float64, d: float64, e: float64, f: float64, g: float64, h: float64,
             x: DD, y: F3, k: s32) -> float64 #foreign;
spill_f4 :: (a: F4, b: F4, c: F4, d: F4, e: F4, f: float32) -> float32 #foreign;
spill_sret :: (a: s32, b: s32, c: s32, d: s32, e: s32, x: S16, y: ID) -> S24 #foreign;
mixed_args :: (a: s8, b: S3, c: float32, d: DD, e: u16, f: ID, g: bool, h: F5) -> float64 #foreign;
with_empty :: (a: Empty, x: s32, b: Empty) -> Empty #foreign;
empty_count :: () -> s32 #foreign;

// C calls these back.
call_s3  :: (cb: (S3, s32) -> S3 #c_call, x: S3) -> S3 #foreign;
call_s12 :: (cb: (S12, s32) -> S12 #c_call, x: S12) -> S12 #foreign;
call_s24 :: (cb: (S24, s32) -> S24 #c_call, x: S24) -> S24 #foreign;
call_s72 :: (cb: (S72, s32) -> S72 #c_call, x: S72) -> S72 #foreign;
call_dd  :: (cb: (DD, s32) -> DD #c_call, x: DD) -> DD #foreign;
call_f3  :: (cb: (F3, s32) -> F3 #c_call, x: F3) -> F3 #foreign;
call_f5  :: (cb: (F5, s32) -> F5 #c_call, x: F5) -> F5 #foreign;
call_id  :: (cb: (ID, s32) -> ID #c_call, x: ID) -> ID #foreign;
call_nested :: (cb: (Nested, s32) -> Nested #c_call, x: Nested) -> Nested #foreign;
call_ufi :: (cb: (UFI, s32) -> UFI #c_call, x: UFI) -> UFI #foreign;
call_spill :: (cb: (s32, s32, s32, s32, s32, s32, S16, DD, F4) -> s64 #c_call) -> s64 #foreign;
call_empty :: (cb: (Empty, s32, Empty) -> Empty #c_call, x: s32) -> s32 #foreign;
c_calls_exported :: () -> S24 #foreign;

cb_s3 :: (x: S3, k: s32) -> S3 #c_call {
    print("  cb_s3 got %\n", x);
    return .{x.a + cast(s8) k, x.b + cast(s8) k, x.c + cast(s8) k};
}
cb_s12 :: (x: S12, k: s32) -> S12 #c_call {
    print("  cb_s12 got %\n", x);
    return .{x.a + k, x.b + k, x.c + k};
}
cb_s24 :: (x: S24, k: s32) -> S24 #c_call {
    print("  cb_s24 got %\n", x);
    return .{x.a + k, x.b + k, x.c + k};
}
cb_s72 :: (x: S72, k: s32) -> S72 #c_call {
    print("  cb_s72 got %\n", x);
    r := x;
    for * r.a  it.* *= k;
    return r;
}
cb_dd :: (x: DD, k: s32) -> DD #c_call {
    print("  cb_dd got %\n", x);
    return .{x.a * cast(float64) k, x.b * cast(float64) k};
}
cb_f3 :: (x: F3, k: s32) -> F3 #c_call {
    print("  cb_f3 got %\n", x);
    return .{x.c, x.b, x.a + cast(float32) k};
}
cb_f5 :: (x: F5, k: s32) -> F5 #c_call {
    print("  cb_f5 got %\n", x);
    return .{x.e, x.d, x.c, x.b, x.a * cast(float32) k};
}
cb_id :: (x: ID, k: s32) -> ID #c_call {
    print("  cb_id got %\n", x);
    return .{cast(s8) (x.a * k), x.d + cast(float64) k};
}
cb_nested :: (x: Nested, k: s32) -> Nested #c_call {
    print("  cb_nested got %\n", x);
    r := x;
    r.name[0] = #char "J";
    r.inner.x += cast(s16) k;
    r.pts[1].y -= cast(s16) k;
    r.f[0] *= cast(float32) k;
    return r;
}
cb_ufi :: (x: UFI, k: s32) -> UFI #c_call {
    print("  cb_ufi got %\n", x.i);
    r: UFI;
    r.i = x.i * k;
    return r;
}
cb_spill :: (a: s32, b: s32, c: s32, d: s32, e: s32, f: s32, x: S16, y: DD, z: F4) -> s64 #c_call {
    print("  cb_spill got % % % % % % % % %\n", a, b, c, d, e, f, x, y, z);
    return a + b + c + d + e + f + x.a + x.b + x.c + x.d + cast(s64) (y.a + y.b + z.a + z.b + z.c + z.d);
}
cb_empty :: (a: Empty, x: s32, b: Empty) -> Empty #c_call {
    print("  cb_empty got %\n", x);
    return .{};
}

// Called by C under these names.
jai_combine :: (a: S12, b: DD, c: S3) -> S24 #c_call #program_export {
    print("  jai_combine got % % %\n", a, b, c);
    return .{a.a + a.b + a.c, cast(s64) (b.a + b.b), c.a + c.b + c.c};
}
jai_tiny :: (x: S1, y: F1) -> S1 #c_call #program_export "jai_tiny_renamed" {
    print("  jai_tiny got % %\n", x, y);
    return .{cast(s8) (x.a + cast(s8) y.a)};
}

main :: () {
    print("small integer structs\n");
    print("%\n", bump_s1(.{-5}));
    print("%\n", bump_s2(.{-300}));
    print("%\n", bump_s3(.{1, 2, -3}));
    print("%\n", bump_s4(.{100000}));
    print("%\n", bump_s8(.{1, -2}));
    print("%\n", bump_s12(.{10, 20, 30}));
    print("%\n", bump_s16(.{1, 2, 3, 4}));
    print("larger structs\n");
    print("%\n", bump_s24(.{1 << 40, -1, 7}));
    s64: S64;
    for 0..15  s64.a[it] = cast(s32) (it * 3);
    print("%\n", bump_s64(s64));
    s72: S72;
    for 0..17  s72.a[it] = cast(s32) (100 - it);
    print("%\n", bump_s72(s72));
    print("int and float\n");
    print("%\n", bump_fi(.{1.5, 7}));
    print("%\n", bump_dd(.{2.25, -8}));
    print("%\n", bump_id(.{-9, 0.75}));
    print("homogeneous float aggregates\n");
    print("%\n", bump_f1(.{1}));
    print("%\n", bump_f2(.{1, 2}));
    print("%\n", bump_f3(.{1, 2, 3}));
    print("%\n", bump_f4(.{1, 2, 3, 4}));
    print("%\n", bump_f5(.{1, 2, 3, 4, 5}));
    print("%\n", bump_d4(.{0.5, 1.5, 2.5, 3.5}));
    print("bool, enum, pointer\n");
    target: s32 = 41;
    m := bump_mixed(.{false, .GREEN, 65535, *target});
    print("% % % % %\n", m.flag, m.color, m.small, m.p == *target, target);
    print("nested and arrays\n");
    n: Nested;
    n.name = .[#char "a", #char "b", #char "c"];
    n.inner = .{-1, 2};
    n.pts = .[Inner.{3, 4}, Inner.{5, 6}];
    n.f = .[0.5, -0.5];
    print("%\n", bump_nested(n));
    print("union\n");
    u: UFI;
    u.i = 1234;
    print("%\n", bump_ufi(u).i);

    print("spilling to the stack\n");
    print("%\n", spill_s16(1, 2, 3, 4, 5, 6, .{7, 8, 9, 10}, 11));
    print("%\n", spill_s8x2(1, 2, 3, 4, 5, 6, .{7, 8}, .{9, 10}));
    print("%\n", split_s12(1, 2, 3, .{4, 5, 6}, .{7, 8, 9}));
    print("%\n", spill_dd(1, 2, 3, 4, 5, 6, 7, 8, .{9, 10}, .{11, 12, 13}, 14));
    print("%\n", spill_f4(.{1, 2, 3, 4}, .{5, 6, 7, 8}, .{9, 10, 11, 12}, .{13, 14, 15, 16}, .{17, 18, 19, 20}, 21));
    print("%\n", spill_sret(1, 2, 3, 4, 5, .{6, 7, 8, 9}, .{10, 11.5}));
    print("%\n", mixed_args(-1, .{2, 3, 4}, 5.5, .{6, 7}, 8, .{9, 10.5}, true, .{11, 12, 13, 14, 15}));
    with_empty(.{}, 3, .{});
    print("empty structs: % calls\n", empty_count());

    print("callbacks\n");
    print("%\n", call_s3(cb_s3, .{1, 2, 3}));
    print("%\n", call_s12(cb_s12, .{1, 2, 3}));
    print("%\n", call_s24(cb_s24, .{1, 2, 3}));
    print("%\n", call_s72(cb_s72, s72));
    print("%\n", call_dd(cb_dd, .{1.5, 2.5}));
    print("%\n", call_f3(cb_f3, .{1, 2, 3}));
    print("%\n", call_f5(cb_f5, .{1, 2, 3, 4, 5}));
    print("%\n", call_id(cb_id, .{3, 4.5}));
    print("%\n", call_nested(cb_nested, n));
    print("%\n", call_ufi(cb_ufi, u).i);
    print("%\n", call_spill(cb_spill));
    print("%\n", call_empty(cb_empty, 9));
    print("exported\n");
    print("%\n", c_calls_exported());

    print("#c_call procedures called from Jai\n");
    print("%\n", cb_s12(.{4, 5, 6}, 10));
    f: (F5, s32) -> F5 #c_call = cb_f5;
    print("%\n", f(.{1, 2, 3, 4, 5}, 2));
    print("%\n", jai_combine(.{1, 2, 3}, .{4, 5}, .{6, 7, 8}));
}
