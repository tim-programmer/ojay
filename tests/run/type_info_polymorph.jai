// Type_Info_Struct of a polymorphic struct instance: polymorph_source_struct,
// specified_parameters and constant_storage.
#import "Basic";
Pair :: struct (A: Type, N: int, NAME := "pair") { a: [N] A; }
Plain :: struct { x: int; }
describe :: (info: *Type_Info_Struct) -> string {
    b: String_Builder;
    print_to_builder(*b, "% source=%", info.name, ifx info.polymorph_source_struct then info.polymorph_source_struct.name else "null");
    for info.specified_parameters {
        p := info.constant_storage.data + it.offset_in_bytes;
        print_to_builder(*b, " %:", it.name);
        if it.type.type == .TYPE         print_to_builder(*b, "%", (cast(*Type) p).*);
        else if it.type.type == .INTEGER print_to_builder(*b, "%", (cast(*int) p).*);
        else if it.type.type == .STRING  print_to_builder(*b, "\"%\"", (cast(*string) p).*);
    }
    print_to_builder(*b, " (% bytes)", info.constant_storage.count);
    return builder_to_string(*b);
}
main :: () {
    print("%\n", describe(type_info(Pair(float32, 3))));
    print("%\n", describe(type_info(Pair(u8, 2, "named"))));
    print("%\n", describe(type_info(Plain)));
    print("%\n", type_info(Pair(int, 1)).polymorph_source_struct == type_info(Pair(u8, 7)).polymorph_source_struct);
    print("%\n", #run describe(type_info(Pair(s16, 4))));
}
