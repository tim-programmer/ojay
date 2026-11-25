// #run: compile-time execution. The results are constants of the compiled
// program; polymorphic instances that only #run needs are not compiled into it.
// COMPILE_OUTPUT: compile time: 3 setups, fib(15) = 610
// COMPILE_OUTPUT: compile time: table[1] = 0x77073096
// COMPILE_OUTPUT: compile time: {[1, 2, 3], hi, GREEN} in main
#import "Basic";
#import "Math";

// ---- a lookup table built at compile time (CRC-32)
make_crc_table :: () -> [256]u32 {
    table: [256]u32;
    for i: 0..255 {
        c := cast(u32) i;
        for 1..8 {
            if c & 1  c = 0xEDB8_8320 ^ (c >> 1);
            else      c >>= 1;
        }
        table[i] = c;
    }
    return table;
}
CRC_TABLE :: #run make_crc_table();
#assert CRC_TABLE[1] == 0x7707_3096;

crc32 :: (s: string) -> u32 {
    c: u32 = 0xFFFF_FFFF;
    for i: 0..s.count-1  c = CRC_TABLE[(c ^ s[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFF_FFFF;
}

// ---- a sine table (floats)
make_sines :: () -> [8]float {
    t: [8]float;
    for * t  it.* = sin(cast(float) it_index * TAU / 8);
    return t;
}
SINES :: #run make_sines();

// ---- results used where constants are needed
fib :: (n: int) -> int { if n < 2 return n; return fib(n - 1) + fib(n - 2); }
N :: #run fib(6);                          // 8
buffer: [N]u8;                             // an array size
#assert N == 8;
#assert #run fib(10) == 55;
#if #run fib(3) == 2 {
    STATIC_IF :: "taken";
} else {
    STATIC_IF :: "not taken";
}
repeat :: ($COUNT: int, x: int) -> int {  // a $N argument
    s := 0;
    for 1..COUNT  s = s * 10 + x;
    return s;
}
Config :: struct {
    level := #run fib(7);                  // a member default
    name := #run sprint("cfg%", 1 + 1);
}
global_level := #run fib(8) * 2;           // a global initializer

// ---- structs, enums, strings, Type, nested and polymorphic results
Color :: enum u8 { RED; GREEN; BLUE; }
Inner :: struct { values: [3]int; label: string; color: Color; }
Outer :: struct { inner: Inner; ok: bool; scale: float64; kind: Type; }
make_outer :: () -> Outer {
    o: Outer;
    for * o.inner.values  it.* = it_index + 1;
    o.inner.label = "hi";
    o.inner.color = .GREEN;
    o.ok = true;
    o.scale = 0.25;
    o.kind = [4]u16;
    return o;
}
OUTER :: #run make_outer();
#assert OUTER.inner.values[2] == 3;

Vector :: struct ($T: Type, $N: int) { data: [N]T; }
iota :: ($T: Type, $N: int) -> Vector(T, N) {
    v: Vector(T, N);
    for 0..N-1  v.data[it] = cast(T) (it * it);
    return v;
}
SQUARES :: #run iota(s16, 5);
pick_type :: () -> Type { if size_of(*void) == 8 return u64; return u32; }
PICKED_TYPE :: #run pick_type();

// ---- operator overloading, defer, switch, dynamic arrays, pointers
V2 :: struct { x, y: int; }
operator + :: (a: V2, b: V2) -> V2 { return .{a.x + b.x, a.y + b.y}; }
operator * :: (a: V2, k: int) -> V2 #symmetric { return .{a.x * k, a.y * k}; }

Node :: struct { value: int; next: *Node; }

busy_work :: () -> int {
    total := 0;
    {
        defer total *= 2;                  // runs at the end of this block
        total += 1;
    }
    nums: [..]int;
    for 1..10  array_add(*nums, it);
    for nums {
        if it == 3 continue;
        if it == 8 break;
        if it % 2 == {
            case 0; total += it;
            case 1; total += 100;
        }
    }
    // a linked list on the compile-time heap (never part of the result)
    head: *Node;
    for 1..4 {
        n := New(Node);
        n.value = it;
        n.next = head;
        head = n;
    }
    n := head;
    while n {
        total += n.value * 1000;
        n = n.next;
    }
    v := V2.{1, 2} + 3 * V2.{10, 20};
    total += v.x + v.y;
    p := *v.y;
    p.* += 1;
    return total + v.y;                    // 2 + 312 (loop) + 10000 (list) + 93 (v) + 63 = 10470
}
BUSY :: #run busy_work();

// ---- globals: #run sees their initial values; its writes are not kept
counter := 5;
bump :: () -> int { counter += 10; return counter; }
BUMPED :: #run bump();
BUMPED_AGAIN :: #run bump();

// ---- the target's layout: size_of, pointer arithmetic and struct offsets
Mixed :: struct { a: u8; p: *void; b: u16; s: string; }
layout :: () -> [4]int {
    m: Mixed;
    base := cast(int) *m;
    return .[size_of(Mixed), cast(int) *m.p - base, cast(int) *m.b - base, cast(int) *m.s - base];
}
LAYOUT :: #run layout();
POINTER_SIZE :: #run size_of(*void);

// ---- polymorphic procedures used only at compile time, or at both times
twice :: (x: $T) -> T { return add(x, x); }
add :: (a: $T, b: T) -> T { return a + b; }
TWICE_INT :: #run twice(21);               // twice(int) and add(int): first needed here, then at run time
only_ct :: (x: $T) -> T { return x * 3; }
TRIPLE :: #run only_ct(7);                 // only_ct(int) is never needed at run time

// ---- #run statements: executed during compilation, for their side effects
setups := 0;
announce :: () {
    setups += 3;                           // a write to a global: local to this #run
    print("compile time: % setups, fib(15) = %\n", setups, fib(15));
}
#run announce();
#run print("compile time: table[1] = 0x%\n", hex(CRC_TABLE[1]));

hex :: (v: u32) -> string {
    digits :: "0123456789abcdef";
    s: string;
    s.data = alloc(8);
    s.count = 8;
    for i: 0..7  s[i] = digits[(v >> cast(u32) (28 - 4 * i)) & 0xF];
    return s;
}

main :: () {
    #run print("compile time: % in main\n", OUTER.inner);
    print("crc32(\"123456789\") = %\n", crc32("123456789"));
    print("sines: % % %\n", SINES[0], SINES[2], SINES[6]);
    print("N = %, buffer.count = %, #if %\n", N, buffer.count, STATIC_IF);
    print("repeat = %\n", repeat(N / 2, 7));
    c: Config;
    print("config = % %, global_level = %\n", c.level, c.name, global_level);
    print("outer = %\n", OUTER);
    print("outer.inner.label = %, kind = %\n", OUTER.inner.label, OUTER.kind);
    print("squares = %, type = %\n", SQUARES, type_of(SQUARES));
    print("picked: pointer-sized = %, matches = %\n", size_of(PICKED_TYPE) == size_of(*void), PICKED_TYPE == ifx size_of(*void) == 8 then u64 else u32);
    print("busy = %\n", BUSY);
    i := 2;
    print("bumped = % %, counter = %\n", BUMPED, BUMPED_AGAIN, counter);
    m: Mixed;
    base := cast(int) *m;
    runtime_layout := int.[size_of(Mixed), cast(int) *m.p - base, cast(int) *m.b - base, cast(int) *m.s - base];
    for LAYOUT  if it != runtime_layout[it_index]  print("layout mismatch at %\n", it_index);
    print("layout matches: %, pointer size matches: %\n", LAYOUT.count == 4, POINTER_SIZE == size_of(*void));
    print("twice = %, twice(2.5) = %, twice(i) = %, triple = %\n", TWICE_INT, twice(2.5), twice(i), TRIPLE);
    print("runtime index: % %\n", CRC_TABLE[i] == 0xEE0E_612C, SQUARES.data[i + 1]);
    total := 0;
    for CRC_TABLE  total += it & 1;
    print("odd entries: %\n", total);
}
