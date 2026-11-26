// for_expansion: `for` over a value calls the #expand macro for_expansion,
// which declares `it / `it_index and inserts the body (For_Flags for `for *`
// and `for <`; `remove it` through #insert (remove = ...)). Also `remove it`
// in for loops over arrays. Everything runs under #run too.
// COMPILE_OUTPUT: #run: 10 20 30 | 30 20 10 | removed [10, 30] | first big 30 | array [1, 2, 10, 4, 5, 8, 7]
#import "Basic";

// ---- a polymorphic list with a for_expansion

List :: struct ($T: Type) {
    items: [16] T;
    count: int;
}

push :: (list: *List($T), item: T) {
    list.items[list.count] = item;
    list.count += 1;
}

for_expansion :: (list: *List($T), body: Code, flags: For_Flags) #expand {
    #if flags & .REVERSE {
        i := list.count - 1;
        while i >= 0 {
            defer i -= 1;
            #if flags & .POINTER  `it := *list.items[i];
            else                  `it := list.items[i];
            `it_index := i;
            #insert (remove = { list.items[i] = list.items[list.count - 1]; list.count -= 1; }) body;
        }
    } else {
        i := 0;
        while i < list.count {
            defer i += 1;
            #if flags & .POINTER  `it := *list.items[i];
            else                  `it := list.items[i];
            `it_index := i;
            #insert (remove = { list.items[i] = list.items[list.count - 1]; list.count -= 1; i -= 1; }) body;
        }
    }
}

// ---- a for_expansion that makes values: a range with a step, without remove

Stepped :: struct { from, to, step: int; }

for_expansion :: (s: Stepped, body: Code, flags: For_Flags) #expand {
    `it := s.from;
    `it_index := 0;
    while `it <= s.to {
        #insert body;
        `it += s.step;
        `it_index += 1;
    }
}

list_text :: (list: *List(int)) -> string {
    builder: String_Builder;
    append(*builder, "[");
    for list.* { if it_index  append(*builder, ", "); print_to_builder(*builder, "%", it); }
    append(*builder, "]");
    result := builder_to_string(*builder);
    free_buffers(*builder);
    return result;
}

first_above :: (list: *List(int), limit: int) -> int {
    for list.*  if it > limit  return it;  // returns from first_above
    return -1;
}

compile_time :: () -> int {
    list: List(int);
    push(*list, 10);
    push(*list, 20);
    push(*list, 30);
    print("#run:");
    for list  print(" %", it);
    print(" |");
    for < list  print(" %", it);
    for list  if it == 20  remove it;
    text := list_text(*list);
    print(" | removed % | first big %", text, first_above(*list, 25));
    free(text);
    a: [..] int;
    for 1..10  array_add(*a, it);
    for a  if it % 3 == 0  remove it;
    print(" | array %\n", a);
    array_free(a);
    return list.count;
}

COMPILE_TIME :: #run compile_time();

main :: () {
    list: List(int);
    for 1..6  push(*list, it * 10);

    print("forward:");
    for list  print(" %", it);
    print("\nnamed:");
    for value, index: list  print(" [%]=%", index, value);
    print("\nreverse:");
    for < list  print(" %", it);
    print("\nby pointer:");
    for * list  it.* += 1;
    for list  print(" %", it);
    print("\nbreak and continue:");
    for list {
        if it == 31  continue;
        if it == 51  break;
        print(" %", it);
    }
    print("\nreturn from the body: %, %\n", first_above(*list, 35), first_above(*list, 100));

    for list  if it % 20 == 1  remove it;
    text := list_text(*list);
    print("removed: % (count %)\n", text, list.count);
    free(text);
    for < list  if it == 11  remove it;
    text = list_text(*list);
    print("removed in reverse: %\n", text);
    free(text);

    // Nested loops over the same list, and break out of the outer one.
    pairs := 0;
    for outer: list {
        for inner: list {
            if inner == outer  continue;
            pairs += 1;
            if pairs == 2  break outer;
        }
    }
    print("pairs before break outer: %\n", pairs);

    // A for_expansion that takes the value itself.
    print("stepped:");
    for Stepped.{1, 10, 3}  print(" %:%", it_index, it);
    print("\nstepped named:");
    for n: Stepped.{0, 20, 5}  print(" %", n);
    print("\n");

    // Polymorphic: a list of strings.
    words: List(string);
    push(*words, "alpha");
    push(*words, "beta");
    push(*words, "gamma");
    for words  if it == "beta"  remove it;
    print("words:");
    for w, i: words  print(" %=%", i, w);
    print("\n");

    // remove it in for loops over arrays (dynamic arrays and views).
    a: [..] int;
    for 1..10  array_add(*a, it);
    for a  if it % 3 == 0  remove it;
    print("array: %\n", a);
    for < a  if it % 2 == 0  remove it;
    print("array reverse: %\n", a);
    v: [] int = a;
    for x: v  if x == 1  remove x;
    print("view: % (the array still has %)\n", v, a.count);
    names: [..] string;
    array_add(*names, "keep");
    array_add(*names, "drop");
    array_add(*names, "drop");
    array_add(*names, "keep too");
    for * names  if it.* == "drop"  remove it;
    print("names: %\n", names);
    array_free(a);
    array_free(names);

    print("COMPILE_TIME: %\n", COMPILE_TIME);
}
