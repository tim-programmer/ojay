// Runtime type information: type_info(T), Type_Info and the Type_Info_*
// structs, Type <-> *Type_Info, and a printer written in Jai that walks them
// (also under #run, where Type_Info lives in compile-time memory).
// COMPILE_OUTPUT: #run: {value = -3, name = "root", next = null, x = 1.5, y = 2, color = BLUE, tags = [1, 2, 3]}
// COMPILE_OUTPUT: #run: Node has 6 members, Color has 3 names
#import "Basic";

Color :: enum u8 { RED; GREEN :: 5; BLUE; }
Access :: enum_flags u32 { READ; WRITE; EXEC; }

Node :: struct {
    value: s32;
    name: string;
    next: *Node;
    using pos: struct { x, y: float; };
    color: Color;
    tags: [3]u16;
}

Shape :: union { radius: float; side: s32; }

// ---- a printer written in Jai, driven by Type_Info

show :: (builder: *String_Builder, value: Any) {
    show_value(builder, value.value_pointer, value.type);
}

show_value :: (builder: *String_Builder, data: *void, info: *Type_Info) {
    if info.type == {
      case .INTEGER;
        i := cast(*Type_Info_Integer) info;
        n: s64;
        if info.runtime_size == {
          case 1; n = ifx i.signed then cast(s64) (cast(*s8) data).* else cast(s64) (cast(*u8) data).*;
          case 2; n = ifx i.signed then cast(s64) (cast(*s16) data).* else cast(s64) (cast(*u16) data).*;
          case 4; n = ifx i.signed then cast(s64) (cast(*s32) data).* else cast(s64) (cast(*u32) data).*;
          case 8; n = (cast(*s64) data).*;
        }
        print_to_builder(builder, "%", n);
      case .FLOAT;
        if info.runtime_size == 4  print_to_builder(builder, "%", (cast(*float32) data).*);
        else                       print_to_builder(builder, "%", (cast(*float64) data).*);
      case .BOOL;
        append(builder, ifx (cast(*bool) data).* then "true" else "false");
      case .STRING;
        print_to_builder(builder, "\"%\"", (cast(*string) data).*);
      case .POINTER;
        p := (cast(**void) data).*;
        if p  append(builder, "pointer");
        else  append(builder, "null");
      case .ENUM;
        e := cast(*Type_Info_Enum) info;
        v: s64;
        if info.runtime_size == 1  v = (cast(*u8) data).*;
        else if info.runtime_size == 4  v = (cast(*u32) data).*;
        else  v = (cast(*s64) data).*;
        for e.names  if e.values[it_index] == v { append(builder, it); return; }
        print_to_builder(builder, "%", v);
      case .ARRAY;
        a := cast(*Type_Info_Array) info;
        count := a.array_count;
        items := data;
        if a.array_type != .FIXED {
            view := cast(*[] u8) data;
            count = view.count;
            items = view.data;
        }
        append(builder, "[");
        for i: 0..count-1 {
            if i  append(builder, ", ");
            show_value(builder, cast(*u8) items + i * a.element_type.runtime_size, a.element_type);
        }
        append(builder, "]");
      case .STRUCT;
        append(builder, "{");
        first := true;
        show_members(builder, data, cast(*Type_Info_Struct) info, *first);
        append(builder, "}");
      case;
        print_to_builder(builder, "<%>", cast(Type) info);
    }
}

show_members :: (builder: *String_Builder, data: *void, s: *Type_Info_Struct, first: *bool) {
    for s.members {
        member := cast(*u8) data + it.offset_in_bytes;
        if it.flags & .ANONYMOUS || (it.flags & .USING && it.type.type == .STRUCT) {
            show_members(builder, member, cast(*Type_Info_Struct) it.type, first);  // shown inline
            continue;
        }
        if !first.*  append(builder, ", ");
        first.* = false;
        print_to_builder(builder, "% = ", it.name);
        show_value(builder, member, it.type);
        if s.textual_flags & .UNION  break;  // a union: its first member
    }
}

to_text :: (value: Any) -> string {
    builder: String_Builder;
    show(*builder, value);
    result := builder_to_string(*builder);
    free_buffers(*builder);
    return result;
}

make_node :: () -> Node {
    n: Node;
    n.value = -3;
    n.name = "root";
    n.x = 1.5;
    n.y = 2;
    n.color = .BLUE;
    n.tags = .[1, 2, 3];
    return n;
}

compile_time :: () -> int {
    n := make_node();
    text := to_text(n);
    print("#run: %\n", text);
    free(text);
    s := type_info(Node);
    e := type_info(Color);
    print("#run: Node has % members, Color has % names\n", s.members.count, e.names.count);
    return s.members.count;
}

COMPILE_TIME :: #run compile_time();

// ---- what type_info reports

describe :: (t: Type) {
    info := cast(*Type_Info) t;
    print("%: %", t, info.type);
    if info.type == {
      case .INTEGER;
        print(" signed=%", (cast(*Type_Info_Integer) info).signed);
      case .POINTER;
        print(" to %", cast(Type) (cast(*Type_Info_Pointer) info).pointer_to);
      case .ARRAY;
        a := cast(*Type_Info_Array) info;
        print(" % of %, count %", a.array_type, cast(Type) a.element_type, a.array_count);
      case .ENUM;
        e := cast(*Type_Info_Enum) info;
        print(" % (%, base %):", e.name, e.enum_type_flags, cast(Type) e.internal_type);
        for e.names  print(" %=%", it, e.values[it_index]);
      case .PROCEDURE;
        p := cast(*Type_Info_Procedure) info;
        print(" (");
        for p.argument_types { if it_index  print(", "); print("%", cast(Type) it); }
        print(") ->");
        for p.return_types  print(" %", cast(Type) it);
        print(", %", p.procedure_flags);
    }
    print("\n");
}

Callback :: #type (a: int, rest: ..string) -> (bool, float);
C_Callback :: #type (x: s32) -> s32 #c_call;

main :: () {
    for Type.[s8, u16, s64, float32, float64, bool, string, Any, Type, void, *Node, **u8, [4]s32, []string,
              [..]Color, Color, Access, Callback, C_Callback]
        describe(it);

    // Struct members: names, types, flags; offsets and sizes agree with the compiler's.
    s := type_info(Node);
    print("Node: % (% members, union %)\n", s.name, s.members.count, (s.textual_flags & .UNION) != 0);
    sizes_ok := s.runtime_size == size_of(Node);
    n: Node;
    base := cast(*u8) *n;
    offsets := s64.[cast(*u8) *n.value - base, cast(*u8) *n.name - base, cast(*u8) *n.next - base,
                    cast(*u8) *n.pos - base, cast(*u8) *n.color - base, cast(*u8) *n.tags - base];
    for s.members {
        print("  % : % [%]\n", it.name, cast(Type) it.type, it.flags);
        if it.offset_in_bytes != offsets[it_index]  sizes_ok = false;
    }
    print("offsets and sizes match: %\n", sizes_ok);
    u := type_info(Shape);
    print("Shape: union %, members %, size ok %\n", (u.textual_flags & .UNION) != 0, u.members.count,
          u.runtime_size == size_of(Shape));
    print("sizes: % % % %\n", type_info(*Node).runtime_size == size_of(*Node), type_info([]int).runtime_size == size_of([]int),
          type_info(string).runtime_size == size_of(string), type_info(Callback).runtime_size == size_of(Callback));

    // Type and *Type_Info.
    a: Any = n.color;
    print("a.type == Color: %, a.type.type: %, type_info(Color) == a.type: %\n", a.type == Color, a.type.type,
          type_info(Color) == a.type);
    generic: *Type_Info = type_info(Node);  // a Type_Info_* pointer converts to *Type_Info
    back: Type = generic;                    // and *Type_Info to Type
    print("back: %, same: %, int as *Type_Info: %\n", back, back == Node, (cast(*Type_Info) int).runtime_size);
    print("member type is *Node: %\n", s.members[2].type == type_info(*Node));

    // The printer.
    n = make_node();
    text := to_text(n);
    print("show: %\n", text);
    free(text);
    values: [..] Any;
    array_add(*values, 42);
    array_add(*values, cast(u8) 200);
    array_add(*values, "str");
    array_add(*values, Access.READ | .EXEC);
    array_add(*values, Shape.{radius = 0.5});
    array_add(*values, int.[7, 8]);
    for values {
        text = to_text(it);
        print("show %: %\n", it.type, text);
        free(text);
    }
    print("COMPILE_TIME: %\n", COMPILE_TIME);
}
