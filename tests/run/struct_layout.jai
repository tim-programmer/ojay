// #place (overlapping members) and #align (member alignment): sizes,
// offsets, Type_Info, printing, literals, defaults, #run results, and the
// alignment of locals, globals and heap allocations. The values printed are
// the same on every target.
// COMPILE_OUTPUT: run: Overlay 8 [a@0 b@0 lo@0 hi@4] Aligned 32 [tag@0 x@16 y@20] ALIGNED true
// COMPILE_OUTPUT: run: Big 64 [a@0 v@32] Outer 48 [c@0 inner@16] Al(8) 16 [c@0 x@8]
// COMPILE_OUTPUT: run: lo 0 hi 1072693248 aligned local true heap true array true arena true bump true (4 aligned requests)
// COMPILE_OUTPUT: run: {1069547520, 1.5, 3} {8589934593, 0, 1, 2}
// COMPILE_OUTPUT: run: vars true true dynamic true true Marked 8 [x@0 start@4 lo@4 hi@6] {1, 2, 3, 196610}
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
    return sprint("lo % hi % aligned local % heap % array % arena % %", o.lo, o.hi, aligned(*l.v, 32), aligned(*h.v, 32),
                  all, from_arena(), from_bump());
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

// An allocator that honours alignment requests (ALLOCATE_ALIGNED, an ojay
// extension) from a buffer that is only 8-aligned.
Bump :: struct {
    pad: u8;
    mem: [4096] u8 #align 8;
    used: int;
    aligned_requests: int;
}
bump_proc :: (mode: Allocator_Mode, size: s64, old_size: s64, old_memory: *void, data: *void) -> *void {
    b := cast(*Bump) data;
    if mode == {
        case .ALLOCATE;
            p := *b.mem[b.used];
            b.used += (size + 7) & ~7;
            return p;
        case .ALLOCATE_ALIGNED;
            b.aligned_requests += 1;
            start := cast(int) *b.mem[0];
            at := ((start + b.used + old_size - 1) & ~(old_size - 1)) - start;
            b.used = at + size;
            return *b.mem[at];
    }
    return null;
}

from_bump :: () -> string {
    b: Bump;
    a := Allocator.{bump_proc, *b};
    x := New(u8, allocator = a);   // misaligns the next block
    p := New(Big, allocator = a);
    arr := NewArray(3, Big, allocator = a);
    ok := aligned(p, 32) && aligned(arr.data, 32);
    {
        push_allocator(a);   // only for this block (sprint below allocates normally)
        dyn: [..] Big;
        for 1..12  array_add(*dyn, .{});
        for * dyn  ok &= aligned(it, 32);
    }
    t := New(Big, allocator = temporary_allocator);
    ok &= aligned(t, 32);
    return sprint("bump % (% aligned requests)", ok, b.aligned_requests);
}

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

// void members take no space: markers for #place.
Marked :: struct {
    x: u32;
    start: void;
    lo: u16;
    hi: u16;
    #place start;
    whole: u32;
}

layout_marked :: () -> string {
    info := type_info(Marked);
    b: String_Builder;
    init_string_builder(*b);
    print_to_builder(*b, "Marked % [", info.runtime_size);
    for info.members {
        if it.name == "whole"  break;
        if it_index  append(*b, " ");
        print_to_builder(*b, "%@%", it.name, it.offset_in_bytes);
    }
    append(*b, "]");
    return builder_to_string(*b);
}

// #align on variables, and dynamic arrays of over-aligned elements.
more_checks :: () -> string {
    a: u8;
    v: [4] float32 #align 64;
    w := 7 #align 32;
    dyn: [..] Big;
    for 1..20  array_add(*dyn, .{a = cast(u8) it});
    dyn_ok := true;
    for * dyn  dyn_ok &= aligned(it, 32) && it.a == it_index + 1;
    res: [..] Big;
    array_reserve(*res, 3);
    m := Marked.{1, 2, 3};
    return sprint("vars % % dynamic % % % %", aligned(*v, 64), aligned(*w, 32), dyn_ok, aligned(res.data, 32),
                  layout_marked(), m);
}

#run print("run: %\n", more_checks());

g_aligned: [3] u8 #align 128;
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

    print("more: %\n", more_checks());
    print("global #align 128: %\n", aligned(*g_aligned, 128));
    mk := Marked.{x = 5, lo = 1, hi = 1};
    print("mk = %, whole = %, *mk.start - *mk = %\n", mk, mk.whole, cast(int) (*mk.start) - cast(int) *mk);
    dyn: [..] Big;
    for 1..50  array_add(*dyn, .{});
    ok := true;
    for * dyn  ok &= aligned(it, 32);
    print("dynamic: % (% elements)\n", ok, dyn.count);
    array_free(dyn);
}
