// The message loop: a #run,stallable intercepts the compilation, reads the
// declarations (.TYPECHECKED: names, types, notes) and adds code to the
// program (add_build_string): a test runner for the @test procedures and a
// to_text for each @serialize struct.
// COMPILE_OUTPUT: declared test_add (kind PROCEDURE_HEADER, type ()) notes [test]
// COMPILE_OUTPUT: declared test_mul (kind PROCEDURE_HEADER, type ()) notes [test]
// COMPILE_OUTPUT: declared helper (kind PROCEDURE_HEADER, type (s64) -> s64) notes []
// COMPILE_OUTPUT: declared Point (kind STRUCT, type Type) notes [serialize]
// COMPILE_OUTPUT: declared counter (kind LITERAL, type s64) notes []
// COMPILE_OUTPUT: declared main (kind PROCEDURE_HEADER, type ()) notes []
// COMPILE_OUTPUT: phase TYPECHECKED_ALL_WE_CAN
// COMPILE_OUTPUT: declared to_text (kind PROCEDURE_HEADER, type (Point) -> string) notes []
// COMPILE_OUTPUT: declared run_tests (kind PROCEDURE_HEADER, type ()) notes []
// COMPILE_OUTPUT: phase TYPECHECKED_ALL_WE_CAN
// COMPILE_OUTPUT: complete
#import "Basic";
#import "Compiler";

test_add :: () @test { print("add: %\n", 2 + 2 == 4); }
test_mul :: () @test { print("mul: %\n", 3 * 3 == 9); }
helper :: (x: int) -> int { return x + 1; }
Point :: struct @serialize { x, y: int; }
counter := 10;

#run,stallable {
    w := get_current_workspace();
    compiler_begin_intercept(w);
    tests: [..] string;
    generated := false;
    while true {
        message := compiler_wait_for_message();
        if message.kind == {
            case .TYPECHECKED;
                tc := cast(*Message_Typechecked) message;
                for tc.declarations {
                    decl := it.expression;
                    notes: [..] string;
                    for decl.notes  array_add(*notes, it.text);
                    print("declared % (kind %, type %) notes %\n", decl.name, decl.expression.kind,
                          ifx decl.type then cast(Type) decl.type else void, notes);
                    for decl.notes  if it.text == "test"  array_add(*tests, decl.name);
                    for decl.notes  if it.text == "serialize" && decl.expression.kind == .STRUCT {
                        info := (cast(*Code_Struct) decl.expression).defined_type;
                        b: String_Builder;
                        print_to_builder(*b, "to_text :: (v: %) -> string { return tprint(\"%{", decl.name, decl.name);
                        for info.members {
                            if it_index  append(*b, " ");
                            append(*b, it.name);
                            append(*b, "=%");
                        }
                        append(*b, "}\"");
                        for info.members  print_to_builder(*b, ", v.%", it.name);
                        append(*b, "); }");
                        add_build_string(builder_to_string(*b));
                    }
                }
            case .PHASE;
                phase := cast(*Message_Phase) message;
                print("phase %\n", phase.phase);
                if !generated && phase.phase == .TYPECHECKED_ALL_WE_CAN {
                    generated = true;
                    b: String_Builder;
                    append(*b, "run_tests :: () {\n");
                    for tests  print_to_builder(*b, "    %();\n", it);
                    print_to_builder(*b, "    print(\"% tests\\n\");\n}\n", tests.count);
                    add_build_string(builder_to_string(*b));
                }
            case .COMPLETE;
                print("complete\n");
                break;
        }
    }
    compiler_end_intercept(w);
}

main :: () {
    run_tests();
    print("%\n", helper(counter));
    print("%\n", to_text(Point.{3, 4}));
}
