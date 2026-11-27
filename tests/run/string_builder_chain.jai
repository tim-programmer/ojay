// String_Builder: a chain of buffers, the first inside the builder.
#import "Basic";

allocs := 0;
frees := 0;
counting :: (mode: Allocator_Mode, size: s64, old_size: s64, old: *void, data: *void) -> *void {
    if mode == .ALLOCATE  allocs += 1;
    if mode == .FREE  frees += 1;
    return default_allocator_proc(mode, size, old_size, old, data);
}

buffers :: (b: *String_Builder) -> int {
    n := 0;
    buffer := get_base_buffer(b);
    while buffer { n += 1; buffer = buffer.next; }
    return n;
}

built_at_compile_time :: () -> string {
    b: String_Builder;
    init_string_builder(*b, 8);
    for 1..10  print_to_builder(*b, "%,", it);
    return builder_to_string(*b);
}

main :: () {
    b: String_Builder;
    b.allocator = .{counting, null};
    print("empty: % [%]\n", builder_string_length(*b), builder_to_string(*b));

    // Short text stays in the builder's own buffer.
    append(*b, "short");
    print("% allocs % buffers %\n", builder_string_length(*b), allocs, buffers(*b));

    // Small subsequent buffers: the text spans several.
    init_string_builder(*b, 16);
    for 0..99  append(*b, cast(u8) (#char "a" + it % 26));
    for 1..400  append(*b, "0123456789");
    total := builder_string_length(*b);
    print("length % (% expected) allocs>0 % buffers>1 %\n", total, 100 + 400 * 10,
          allocs > 0, buffers(*b) > 1);
    s := builder_to_string(*b);
    ok := s.count == total;
    for 0..99  ok &= s[it] == cast(u8) (#char "a" + it % 26);
    for i: 0..400 - 1  ok &= s[100 + i * 10 + 7] == #char "7";
    print("text ok %\n", ok);
    free(s);

    // After builder_to_string (a reset) the buffers are reused.
    before := allocs;
    for 1..400  append(*b, "abcdefghij");
    print("reused: % new allocs, length %\n", allocs - before, builder_string_length(*b));
    reset(*b);

    // A large append gets a buffer big enough; ensure_contiguous_space too.
    big: [100] u8;
    for * big  it.* = #char "x";
    append(*b, big.data, big.count);
    print("% contiguous % length %\n", ensure_contiguous_space(*b, 40), get_current_buffer(*b).allocated -
          get_current_buffer(*b).count >= 40, builder_string_length(*b));

    n := write_builder(*b);
    print("\nwrote % now %\n", n, builder_string_length(*b));

    count := buffers(*b) - 1;
    free_buffers(*b);
    print("freed all % left %\n", frees == count && count > 0, buffers(*b) - 1);
    print("%\n", #run built_at_compile_time());
}
