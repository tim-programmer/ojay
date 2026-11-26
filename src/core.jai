// ojay's core declarations: the parts of Jai's Preload that every module
// sees and that need no context (the context and allocators are in
// preload.jai). Using them costs nothing until a program does.

// ------------------------------------------------------------------ for_expansion

// The flags a `for` over a value passes to its for_expansion macro (a
// compile-time constant there): `for *` sets POINTER, `for <` REVERSE.
For_Flags :: enum_flags u32 {
    POINTER :: 0x1;
    REVERSE :: 0x2;
}

// ------------------------------------------------------------------ runtime type information
//
// A `Type` value points to its Type_Info, and type_info(T) returns it as the
// Type_Info_* struct for T's kind. The compiler emits these tables (codegen
// and the #run interpreter fill them in by member name), and the runtime's
// print reads them (runtime/ojay_runtime.c mirrors the layout), so the three
// must agree.
//
// Differences from Jai: enums that Jai nests inside the structs are top-level
// here (Type_Info_Array_Type, Type_Info_Struct_Member_Flags, ...), and there
// are no notes, initializers, polymorph sources or struct parameters.

Type_Info_Tag :: enum u32 {
    INTEGER              :: 0;
    FLOAT                :: 1;
    BOOL                 :: 2;
    STRING               :: 3;
    POINTER              :: 4;
    PROCEDURE            :: 5;
    VOID                 :: 6;
    STRUCT               :: 7;
    ARRAY                :: 8;
    OVERLOAD_SET         :: 9;
    ANY                  :: 10;
    ENUM                 :: 11;
    POLYMORPHIC_VARIABLE :: 12;
    TYPE                 :: 13;
    CODE                 :: 14;
}

Type_Info :: struct {
    type: Type_Info_Tag;
    runtime_size: s64;
}

Type_Info_Integer :: struct {
    using info: Type_Info;
    signed: bool;
}

Type_Info_Float :: struct {
    using info: Type_Info;
}

Type_Info_Pointer :: struct {
    using info: Type_Info;
    pointer_to: *Type_Info;
}

Type_Info_Array_Type :: enum u32 {
    FIXED     :: 0;
    VIEW      :: 1;
    RESIZABLE :: 2;
}

Type_Info_Array :: struct {
    using info: Type_Info;
    element_type: *Type_Info;
    array_type: Type_Info_Array_Type;
    array_count: s64;  // FIXED: the count; -1 otherwise
}

Type_Info_Struct_Member_Flags :: enum_flags u32 {
    CONSTANT        :: 0x1;
    IMPORTED        :: 0x2;
    USING           :: 0x4;   // `using name: T;`
    PROCEDURE_VALUE :: 0x8;
    ANONYMOUS       :: 0x100; // ojay: an anonymous struct or union member (its name is empty)
}

Type_Info_Struct_Member :: struct {
    name: string;
    type: *Type_Info;
    offset_in_bytes: s64;
    flags: Type_Info_Struct_Member_Flags;
}

Struct_Textual_Flags :: enum_flags u32 {
    FOREIGN    :: 0x1;
    UNION      :: 0x2;
    NO_PADDING :: 0x4;
    TYPE_TAG   :: 0x8;
    ALIGNED    :: 0x10;
}

Type_Info_Struct :: struct {
    using info: Type_Info;
    name: string;
    members: [] Type_Info_Struct_Member;
    textual_flags: Struct_Textual_Flags;
}

Enum_Type_Flags :: enum_flags u32 {
    FLAGS     :: 0x1;  // enum_flags
    SPECIFIED :: 0x2;  // #specified (not supported yet: never set)
}

Type_Info_Enum :: struct {
    using info: Type_Info;
    name: string;
    internal_type: *Type_Info_Integer;
    enum_type_flags: Enum_Type_Flags;
    names: [] string;
    values: [] s64;
}

Type_Info_Procedure_Flags :: enum_flags u32 {
    IS_C_CALL   :: 0x1;
    HAS_VARARGS :: 0x2;  // the last argument type is the view the variadic arguments arrive in
}

Type_Info_Procedure :: struct {
    using info: Type_Info;
    argument_types: [] *Type_Info;
    return_types: [] *Type_Info;
    procedure_flags: Type_Info_Procedure_Flags;
}

// ------------------------------------------------------------------ source locations

// What #location(x) and #caller_location give. Files have an absolute path;
// code in the embedded modules reports their names (<Basic>, <Math>, ...),
// and text made by #insert reports where it was inserted.
Source_Code_Location :: struct {
    fully_pathed_filename: string;
    line_number: s64;
    character_number: s64;
}
