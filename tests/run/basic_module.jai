// The part of Basic written in Jai: polymorphic array procedures, strings,
// String_Builder and log (also under #run), and `..array` arguments.
// COMPILE_OUTPUT: #run: [3, 1, 4, 5, 9] | 1+2=3 | sum 22 | found true 2
// COMPILE_OUTPUT: #run log: compile time 7
#import "Basic";

// ---- `..array` arguments

sum :: (xs: .. int) -> int {
    total := 0;
    for xs  total += it;
    return total;
}

poly_sum :: (xs: .. $T) -> T {
    total: T = 0;
    for xs  total += it;
    return total;
}

// A print wrapper: its arguments go on to print and sprint.
tagged :: (tag: string, format: string, args: .. Any) {
    print("[%] ", tag);
    print(format, ..args);
    s := sprint(format, ..args);
    print(" (% bytes)\n", s.count);
    free(s);
}

test_spread :: () {
    fixed := int.[1, 2, 3];
    dyn: [..] int;
    array_add(*dyn, 10);
    array_add(*dyn, 20);
    view: [] int = fixed;
    print("sum: % % % % %\n", sum(..fixed), sum(..dyn), sum(..view), sum(), sum(4, 5));
    floats := float.[0.5, 1.25];
    print("poly_sum: % %\n", poly_sum(..fixed), poly_sum(..floats));
    tagged("info", "% and %", 1, "two");
    none: [] Any;
    tagged("empty", "nothing", ..none);
    array_free(dyn);
}

// ---- arrays

test_arrays :: () {
    a: [..] int;
    for 1..5  array_add(*a, it * 10);
    array_insert_at(*a, 15, 1);
    array_insert_at(*a, 5, 0);
    array_insert_at(*a, 60, a.count);
    print("insert_at: %\n", a);
    array_ordered_remove_by_index(*a, 0);
    print("ordered_remove_by_index: %\n", a);
    array_unordered_remove_by_index(*a, 0);
    print("unordered_remove_by_index: %\n", a);
    found, index := array_find(a, 30);
    missing, none := array_find(a, 31);
    print("find: % % / % %\n", found, index, missing, none);
    added, at := array_add_if_unique(*a, 30);
    added2, at2 := array_add_if_unique(*a, 70);
    print("add_if_unique: % % / % % -> %\n", added, at, added2, at2, a);

    array_add(*a, 20);
    array_add(*a, 20);
    print("ordered_remove_by_value: % -> %\n", array_ordered_remove_by_value(*a, 20), a);
    array_add(*a, 40);
    array_add(*a, 40);
    print("unordered_remove_by_value (first): % -> %\n", array_unordered_remove_by_value(*a, 40, stop_after_first = true), a);
    print("unordered_remove_by_value: % -> %\n", array_unordered_remove_by_value(*a, 40), a);

    array_reserve(*a, 100);
    print("reserve: count % allocated %\n", a.count, a.allocated);
    array_reserve(*a, 10);  // never shrinks
    print("reserve smaller: allocated %\n", a.allocated);
    array_resize(*a, 7);
    print("resize: %\n", a);
    array_resize(*a, 2);
    print("resize smaller: % (allocated %)\n", a, a.allocated);

    print("pop: %, peek: %, peek_pointer: %\n", pop(*a), peek(a), peek_pointer(a).*);
    empty: [..] int;
    print("peek_pointer of empty: %\n", peek_pointer(empty) == null);

    b: [..] int;
    array_copy(*b, int.[7, 8, 9, 10]);
    print("copy: % (count %)\n", b, b.count);
    v := array_view(b, 1, 2);
    print("view: % (count %)\n", v, v.count);
    c := array_copy(v);
    c[0] = 99;
    print("copy of view: % / %\n", c, b);
    free(c);

    // Elements with default values, and strings.
    Item :: struct { name := "none"; amount := 1; }
    items: [..] Item;
    array_resize(*items, 2);
    array_insert_at(*items, Item.{"first", 5}, 0);
    print("items: %\n", items);
    names: [..] string;
    array_add(*names, "b");
    array_add_if_unique(*names, "a");
    array_add_if_unique(*names, "b");
    print("names: %\n", names);

    x, y := 1, 2;
    swap(*x, *y);
    s, t := "left", "right";
    swap(*s, *t);
    print("swap: % % % %\n", x, y, s, t);

    array_free(a);
    array_free(b);
    array_free(items);
    array_free(names);
}

// ---- strings and String_Builder

test_strings :: () {
    c := to_c_string("hello");
    s := to_string(c);
    print("to_string: % (% bytes, strlen %)\n", s, s.count, c_style_strlen(c));
    print("to_string(data, count): %\n", to_string(c + 1, 3));
    copy := copy_string(s);
    free(c);
    print("copy_string: %\n", copy);
    print("compare: % % % % %\n", compare("abc", "abc") == 0, compare("abc", "abd") < 0, compare("b", "abc") > 0,
          compare("ab", "abc") < 0, compare("", "") == 0);
    print("is_digit % %, is_alpha % %, is_alnum % %, is_space % %\n", is_digit(#char "7"), is_digit(#char "x"),
          is_alpha(#char "Q"), is_alpha(#char "1"), is_alnum(#char "_"), is_alnum(#char "-"),
          is_space(#char "\t"), is_space(#char "a"));
    free(copy);

    builder: String_Builder;
    init_string_builder(*builder);
    append(*builder, "x = ");
    print_to_builder(*builder, "%, y = %", 12, 3.5);
    append(*builder, #char ";");
    print("builder length: %\n", builder_string_length(*builder));
    text := builder_to_string(*builder);
    print("builder: %\n", text);
    free(text);
    for 1..100  append(*builder, "0123456789");
    text = builder_to_string(*builder);
    print("long builder: % bytes, ends with %\n", text.count, to_string(text.data + text.count - 3, 3));
    free(text);
    append(*builder, "gone");
    reset(*builder);
    print("after reset: %\n", builder_string_length(*builder));
    free_buffers(*builder);
}

// ---- log

Log_Capture :: struct {
    count: int;
    errors: int;
    last: string;
}

capture_logger :: (message: string, data: *void, info: Log_Info) {
    capture := cast(*Log_Capture) data;
    capture.count += 1;
    if info.common_flags & .ERROR  capture.errors += 1;
    free(capture.last);
    capture.last = copy_string(message);
}

test_log :: () {
    log("to the default logger: %", 1);
    capture: Log_Capture;
    ctx := context;
    ctx.logger = capture_logger;
    ctx.logger_data = *capture;
    push_context ctx {
        log("one %", 1);
        log_error("two %", 2);
    }
    print("captured % (% errors), last: %\n", capture.count, capture.errors, capture.last);
    free(capture.last);
}

// ---- the same at compile time

compile_time :: () -> int {
    a: [..] int;
    array_copy(*a, int.[3, 1, 4, 1, 5]);
    array_ordered_remove_by_index(*a, 3);
    array_add(*a, 9);
    found, index := array_find(a, 4);
    builder: String_Builder;
    print_to_builder(*builder, "%+%=%", 1, 2, 3);
    text := builder_to_string(*builder);
    print("#run: % | % | sum % | found % %\n", a, text, sum(..a), found, index);
    log("#run log: compile time %", 7);
    free(text);
    free_buffers(*builder);
    result := pop(*a);
    array_free(a);
    return result;
}

COMPILE_TIME :: #run compile_time();

main :: () {
    test_spread();
    test_arrays();
    test_strings();
    test_log();
    print("COMPILE_TIME: %\n", COMPILE_TIME);
}
