// Structs with #align members and #place overlaps by value across the C ABI
// (C writes them with _Alignas and anonymous unions; c_abi_layout.c checks
// the layout with _Static_assert), in both directions.
#import "Basic";

Al16    :: struct { tag: u8; x: u8 #align 16; y: s32; }
AlSmall :: struct { x: u8 #align 16; }
AlF     :: struct { a: float32; b: float32 #align 8; }
AlD     :: struct { a: float64 #align 16; b: float64; }
Pl2     :: struct { a: s64; #place a; lo: u32; hi: u32; }
PlF     :: struct { a: float32; #place a; b: float32; c: float32; }
PlD     :: struct { d: float64; #place d; x, y: float32; }
PlB     :: struct { c: s8; #place c; w: s32; s: s16; }

c_al16    :: (v: Al16) -> Al16 #foreign;
c_alsmall :: (v: AlSmall) -> AlSmall #foreign;
c_alf     :: (v: AlF) -> AlF #foreign;
c_ald     :: (v: AlD) -> AlD #foreign;
c_pl2     :: (v: Pl2) -> Pl2 #foreign;
c_plf     :: (v: PlF) -> PlF #foreign;
c_pld     :: (v: PlD) -> PlD #foreign;
c_plb     :: (v: PlB) -> PlB #foreign;
c_call_back :: (f: (s32, s32, s32, Al16) -> Al16 #c_call, g: (float64, PlD) -> PlD #c_call) -> s32 #foreign;
c_aligned_local :: (f: (p: *Al16) -> s32 #c_call) -> s32 #foreign;

jai_al16 :: (a: s32, b: s32, c: s32, v: Al16) -> Al16 #c_call {
    return .{tag = v.tag + cast(u8) a, x = v.x + cast(u8) b, y = v.y + c};
}
jai_pld :: (k: float64, v: PlD) -> PlD #c_call {
    r: PlD;
    r.x = v.x + cast(float32) k;
    r.y = v.y + cast(float32) k;
    return r;
}
jai_check :: (p: *Al16) -> s32 #c_call {
    return ifx (cast(int) p) % 16 == 0 && p.tag == 7 && p.x == 8 && p.y == 9 then 1 else 0;
}

main :: () {
    print("sizes % % % % % % % %\n", size_of(Al16), size_of(AlSmall), size_of(AlF), size_of(AlD), size_of(Pl2),
          size_of(PlF), size_of(PlD), size_of(PlB));
    print("al16 %\n", c_al16(.{tag = 1, x = 2, y = 5}));
    print("alsmall %\n", c_alsmall(.{x = 41}));
    print("alf %\n", c_alf(.{a = 1.5, b = 2.5}));
    print("ald %\n", c_ald(.{a = 3, b = 1}));
    p2 := c_pl2(.{lo = 1, hi = 2});
    print("pl2 a % lo % hi %\n", p2.a, p2.lo, p2.hi);
    plf := PlF.{b = 1.25, c = 4};
    rf := c_plf(plf);
    print("plf a % c %\n", rf.a, rf.c);
    pd: PlD;
    pd.x = 1;
    pd.y = 2;
    rd := c_pld(pd);
    print("pld x % y %\n", rd.x, rd.y);
    rb := c_plb(.{w = 41, s = 10});
    print("plb w % s %\n", rb.w, rb.s);
    print("call back %\n", c_call_back(jai_al16, jai_pld));
    print("aligned local from C %\n", c_aligned_local(jai_check));
}
