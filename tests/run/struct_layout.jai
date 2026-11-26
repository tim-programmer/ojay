// #place (overlapping members) and #align (member alignment): sizes,
// offsets, Type_Info, printing, literals, defaults, #run results, and the
// alignment of locals, globals and heap allocations. The values printed are
// the same on every target.
// COMPILE_OUTPUT: run: Overlay 8 [a@0 b@0 lo@0 hi@4] Aligned 32 [tag@0 x@16 y@20] ALIGNED true
// COMPILE_OUTPUT: run: Big 64 [a@0 v@32] Outer 48 [c@0 inner@16] Al(8) 16 [c@0 x@8]
// COMPILE_OUTPUT: run: lo 0 hi 1072693248 aligned local true heap true array true arena true
// COMPILE_OUTPUT: run: {1069547520, 1.5, 3} {8589934593, 0, 1, 2}
#import "Basic";

Overlay :: struct {
    a: s64;
    #place a;
    b: float64;
    #place a;
    lo: u32;
    hi: u32;
}

Aligned :: struct {
    tag: u8;
    x: u8 #align 16;
    y: s32;
}

Big :: struct {
    a: u8;
    v: [4] float32 #align 32;
}

Outer :: struct {
    c: u8;
    inner: Aligned;
}

Al :: struct ($A: int) {
    c: u8;
    x: u8 #align A;
}

// Overlapping members with one default between them.
Mixed :: struct {
    tag: u32 = 7;
    #place tag;
    f: float32;
    n: s16 = 3;
}

layout :: (t: Type) -> string {
    info := cast(*Type_Info_Struct) t;
    b: String_Builder;
    init_string_builder(*b);
    print_to_builder(*b, "% % [", info.name, info.runtime_size);
    for info.members {
        if it_index  append(*b, " ");
        print_to_builder(*b, "%@%", it.name, it.offset_in_bytes);
    }
    append(*b, "]");
    return builder_to_string(*b);
}

aligned :: (p: *void, n: int) -> bool { return (cast(int) p) % n == 0; }

checks :: () -> string {
    o: Overlay;
    o.b = 1.0;
    l: Big;
    h := New(Big);
    arr := NewArray(3, Big);
    all := true;
    for * arr  all &= aligned(*it.v, 32);
    return sprint("lo % hi % aligned local % heap % array % arena %", o.lo, o.hi, aligned(*l.v, 32), aligned(*h.v, 32),
                  all, from_arena());
}

make_overlay :: () -> Overlay { return .{lo = 1, hi = 2}; }

// An allocator that hands out 64-aligned pieces of its own buffer.
Arena :: struct {
    mem: [1024] u8 #align 64;
    used: int;
}
arena_proc :: (mode: Allocator_Mode, size: s64, old_size: s64, old_memory: *void, data: *void) -> *void {
    a := cast(*Arena) data;
    if mode != .ALLOCATE  return null;
    a.used = (a.used + 63) & ~63;
    p := *a.mem[a.used];
    a.used += size;
    return p;
}
g_arena: Arena;

from_arena :: () -> bool {
    p := New(Big, allocator = .{arena_proc, *g_arena});
    q := New(Big, allocator = .{arena_proc, *g_arena});
    return aligned(p, 64) && aligned(*q.v, 32) && q != p;
}

#run print("run: % % ALIGNED %\n", layout(Overlay), layout(Aligned),
           (type_info(Aligned).textual_flags & .ALIGNED) != 0);
#run print("run: % % %\n", layout(Big), layout(Outer), layout(Al(8)));
#run print("run: %\n", checks());
#run print("run: % %\n", Mixed.{f = 1.5}, make_overlay());

g_big: Big;
g_over := Overlay.{lo = 1, hi = 2};
g_mixed := Mixed.{f = 1.5};
CONST_OVER :: #run make_overlay();

main :: () {
    print("%\n%\n%\n%\n%\n", layout(Overlay), layout(Aligned), layout(Big), layout(Outer), layout(Al(8)));
    print("size_of: Overlay % Aligned % Big % Outer % Mixed %\n", size_of(Overlay), size_of(Aligned), size_of(Big),
          size_of(Outer), size_of(Mixed));
    print("ALIGNED: % %\n", (type_info(Aligned).textual_flags & .ALIGNED) != 0,
          (type_info(Overlay).textual_flags & .ALIGNED) != 0);

    // Writing one member and reading the ones that overlap it.
    o: Overlay;
    o.b = 1.0;
    print("o = % (a %, lo %, hi %)\n", o, o.a, o.lo, o.hi);
    o.lo = 5;
    o.hi = 0;
    print("o.a = %\n", o.a);
    p := *o;
    p.a = -1;
    print("p.lo = %, p.hi = %\n", p.lo, p.hi);

    // Literals and defaults: a literal value replaces the defaults of the
    // members it overlaps.
    m: Mixed;
    print("m = % (tag %)\n", m, m.tag);
    m2 := Mixed.{f = 1.5};
    print("m2 = %\n", m2);
    print("g_mixed = %, g_over = %, CONST_OVER = %\n", g_mixed, g_over, CONST_OVER);
    lit := Overlay.{b = 2.0};
    print("lit.hi = %\n", lit.hi);
    print("make_overlay().a = %\n", make_overlay().a);

    // Alignment of locals, globals and heap values.
    l: Big;
    a: Aligned;
    out: Outer;
    print("local %, global %, nested %, member x %\n", aligned(*l.v, 32), aligned(*g_big.v, 32),
          aligned(*out.inner, 16), aligned(*a.x, 16));
    h := New(Big);
    arr := NewArray(5, Big);
    all := true;
    for * arr  all &= aligned(*it.v, 32);
    print("New %, NewArray %\n", aligned(*h.v, 32), all);
    free(h);
    free(arr.data);
    h2 := New(Big, initialized = false);
    print("New again %\n", aligned(h2, 32));
    print("checks: %\n", checks());
}
