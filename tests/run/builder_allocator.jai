#import "Basic";

counted := 0;
counting :: (mode: Allocator_Mode, size: s64, old_size: s64, old: *void, data: *void) -> *void {
    if mode == .ALLOCATE  counted += 1;
    return default_allocator_proc(mode, size, old_size, old, data);
}

main :: () {
    b: String_Builder;
    print_to_builder(*b, "x = %", 42);
    s := builder_to_string(*b, Allocator.{counting, null}, extra_bytes_to_prepend = 2);
    s[0] = #char "[";
    s[1] = #char " ";
    print("% %\n", s, counted);
    append(*b, "again");
    t := builder_to_string(*b);
    print("% %\n", t, counted);
    append(*b, "temp");
    t2 := builder_to_string(*b, temporary_allocator);
    print("[%] %\n", t2, counted);
}
