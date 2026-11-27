// Module Compiler: the program's code as data, after Jai's module of the same
// name. Everything here runs at compile time only (#compiler procedures, used
// from #run, #insert -> ..., #modify and other #compiler procedures).
//
//   root, expressions := compiler_get_nodes(code);   // Code -> Code_Node tree
//   code := compiler_get_code(root);                 // Code_Node tree -> Code
//
// The nodes are the code as written: ojay does not typecheck a Code value
// before it is inserted, so `type` is null and identifiers are not resolved
// (resolved_declaration is null). Kinds of syntax without a node type of
// their own (struct literals, switches, directives, ...) are Code_Node with
// kind .OTHER: compiler_get_code gives them back unchanged. The nodes live
// in compile-time memory (allocated with the heap, never freed).

#import "Basic";

// ------------------------------------------------------------------ nodes

Code_Node :: struct {
    Kind :: enum u16 {
        OTHER            :: 0;  // ojay: syntax without a node type of its own
        LITERAL          :: 1;
        IDENT            :: 2;
        UNARY_OPERATOR   :: 3;
        BINARY_OPERATOR  :: 4;
        PROCEDURE_CALL   :: 5;
        CAST             :: 6;
        BLOCK            :: 7;
        DECLARATION      :: 8;
        IF               :: 9;
        WHILE            :: 10;
        FOR              :: 11;
        RETURN           :: 12;
        ARRAY_SUBSCRIPT  :: 13;
        IF_EXPRESSION    :: 14;  // `ifx c then a else b`
        BREAK            :: 15;
        CONTINUE         :: 16;
        DEFER            :: 17;
        NOTE             :: 18;  // Code_Note (in a declaration's notes)
        STRUCT           :: 19;  // Code_Struct (a struct declaration's expression, in messages)
        PROCEDURE_HEADER :: 20;  // Code_Procedure_Header (a procedure declaration's expression, in messages)
    }

    kind: Kind;
    node_flags: u32;
    l0, c0: s32;               // where the node starts (line, column)
    l1, c1: s32;               // where it ends (ojay: the same)
    type: *Type_Info;          // null: the code is not typechecked
    enclosing_load: *void;     // null
    ojay_syntax: s64;          // ojay: the syntax this node was made from (for compiler_get_code)
}

// The operators: single-character ones as that character (#char "+"), the
// others as these values.
Operator_Type :: enum u32 {
    ISEQUAL          :: 0x100;  // ==
    ISNOTEQUAL       :: 0x101;  // !=
    LESS_EQUALS      :: 0x102;  // <=
    GREATER_EQUALS   :: 0x103;  // >=
    LOGICAL_AND      :: 0x104;  // &&
    LOGICAL_OR       :: 0x105;  // ||
    SHIFT_LEFT       :: 0x106;  // <<
    SHIFT_RIGHT      :: 0x107;  // >>
    PLUS_EQUALS      :: 0x110;  // +=
    MINUS_EQUALS     :: 0x111;
    TIMES_EQUALS     :: 0x112;
    DIVIDE_EQUALS    :: 0x113;
    MOD_EQUALS       :: 0x114;
    BITWISE_AND_EQUALS :: 0x115;
    BITWISE_OR_EQUALS  :: 0x116;
    BITWISE_XOR_EQUALS :: 0x117;
    SHIFT_LEFT_EQUALS  :: 0x118;
    SHIFT_RIGHT_EQUALS :: 0x119;
    DEREFERENCE      :: 0x120;  // unary: x.*
}

Code_Literal :: struct {
    #as using base: Code_Node;
    value_type: Type_Info_Tag;  // INTEGER, FLOAT, STRING, BOOL, POINTER (null)
    union {
        _u64: u64;
        _s64: s64;
        _float64: float64;
        _string: string;
    }
}

Code_Ident :: struct {
    #as using base: Code_Node;
    name: string;
    resolved_declaration: *Code_Declaration;  // null (not resolved)
}

Code_Unary_Operator :: struct {
    #as using base: Code_Node;
    operator_type: u32;  // - + ! ~, #char "*" (address of), Operator_Type.DEREFERENCE
    subexpression: *Code_Node;
}

// Also member access (`a.b`: operator #char ".", right an identifier; an
// enum value `.B` has no left) and assignments as statements
// (`a = b`: #char "=", `a += b`: Operator_Type.PLUS_EQUALS, ...).
Code_Binary_Operator :: struct {
    #as using base: Code_Node;
    operator_type: u32;
    left: *Code_Node;
    right: *Code_Node;
}

Code_Argument :: struct {
    expression: *Code_Node;
    name: *Code_Ident;  // a named argument's name, else null
}

Code_Procedure_Call :: struct {
    #as using base: Code_Node;
    procedure_expression: *Code_Node;
    arguments_unsorted: [] Code_Argument;  // as written
    arguments_sorted: [] *Code_Node;       // ojay: the same expressions, in the order written
}

Code_Cast :: struct {
    #as using base: Code_Node;
    Flags :: enum_flags u32 {
        AUTO     :: 0x1;  // xx
        TRUNCATE :: 0x2;  // cast,trunc
        NO_CHECK :: 0x4;  // cast,no_check
    }
    target_type: *Code_Node;  // null for xx
    expression: *Code_Node;
    flags: Flags;
}

Code_Array_Subscript :: struct {
    #as using base: Code_Node;
    array_or_pointer_expression: *Code_Node;
    subscript_expression: *Code_Node;
}

Code_If_Expression :: struct {
    #as using base: Code_Node;
    condition: *Code_Node;
    then_expression: *Code_Node;
    else_expression: *Code_Node;
}

Code_Block :: struct {
    #as using base: Code_Node;
    statements: [] *Code_Node;
}

Code_Declaration :: struct {
    #as using base: Code_Node;
    Flags :: enum_flags u32 {
        IS_CONSTANT :: 0x1;  // name :: value
        UNINITIALIZED :: 0x2;  // = ---
    }
    name: string;
    type_inst: *Code_Node;   // the declared type, or null
    expression: *Code_Node;  // the value, or null
    flags: Flags;
    notes: [] *Code_Note;    // `name :: () @note { ... }`, `S :: struct @note { ... }` (in messages)
}

Code_Note :: struct {
    #as using base: Code_Node;
    text: string;  // without the '@'
}

// The expression of a struct declaration in a .TYPECHECKED message.
Code_Struct :: struct {
    #as using base: Code_Node;
    defined_type: *Type_Info_Struct;
}

// The expression of a procedure declaration in a .TYPECHECKED message
// (`type` is its Type_Info_Procedure; null for a polymorphic procedure).
Code_Procedure_Header :: struct {
    #as using base: Code_Node;
    Flags :: enum_flags u32 {
        POLYMORPHIC :: 0x1;
        MACRO       :: 0x2;  // #expand
        FOREIGN     :: 0x4;
        C_CALL      :: 0x8;
        COMPILER    :: 0x10;
    }
    name: string;
    procedure_flags: Flags;
    notes: [] *Code_Note;
}

Code_If :: struct {
    #as using base: Code_Node;
    condition: *Code_Node;
    then_block: *Code_Node;  // a Code_Block, or the one statement written
    else_block: *Code_Node;  // null without an else
}

Code_While :: struct {
    #as using base: Code_Node;
    condition: *Code_Node;
    block: *Code_Node;
}

Code_For :: struct {
    #as using base: Code_Node;
    Flags :: enum_flags u32 {
        POINTER :: 0x1;  // for *
        REVERSE :: 0x2;  // for <
    }
    ident_name: string;                       // "it", or the name given
    index_name: string;                       // "it_index", or the name given
    iteration_expression: *Code_Node;         // the array (or the range's start)
    iteration_expression_right: *Code_Node;   // a range's end, else null
    block: *Code_Node;
    flags: Flags;
}

Code_Return :: struct {
    #as using base: Code_Node;
    arguments_unsorted: [] Code_Argument;
}

Code_Break_Continue :: struct {  // kind BREAK or CONTINUE
    #as using base: Code_Node;
    label: string;  // `break x`: "x"
}

Code_Defer :: struct {
    #as using base: Code_Node;
    statement: *Code_Node;
}

// ------------------------------------------------------------------ the message loop
//
// A `#run,stallable` (at top level) runs once the rest of the program is
// typechecked; it can intercept the compilation and get messages about it:
//
//   #run,stallable {
//       w := get_current_workspace();
//       compiler_begin_intercept(w);
//       while true {
//           message := compiler_wait_for_message();
//           if message.kind == .TYPECHECKED { ... }      // the declarations
//           if message.kind == .COMPLETE  break;
//       }
//       compiler_end_intercept(w);
//   }
//
// Messages: .TYPECHECKED (Message_Typechecked: the main program's top-level
// declarations, then those of each build string), .PHASE (Message_Phase:
// TYPECHECKED_ALL_WE_CAN once everything so far is typechecked) and
// .COMPLETE. Code added with add_build_string during the loop is compiled into
// the program (and reported in turn). ojay compiles one workspace into one
// object, after the loop: there are no other workspaces, and no messages for
// the phases after typechecking.

Workspace :: s64;

Message :: struct {
    Kind :: enum u32 {
        FILE        :: 1;
        IMPORT      :: 2;
        PHASE       :: 3;
        TYPECHECKED :: 4;
        COMPLETE    :: 5;
        ERROR       :: 6;
    }
    kind: Kind;
    workspace: Workspace;
}

Typechecked :: struct (T: Type) {
    expression: *T;
    subexpressions: [] *Code_Node;  // (ojay: empty)
}

Message_Typechecked :: struct {
    #as using base: Message;
    declarations: [] Typechecked(Code_Declaration);
}

Message_Phase :: struct {
    #as using base: Message;
    Phase :: enum u32 {
        ALL_SOURCE_CODE_PARSED :: 0;
        TYPECHECKED_ALL_WE_CAN :: 1;
        ALL_TARGET_CODE_BUILT  :: 2;
        PRE_WRITE_EXECUTABLE   :: 3;
        POST_WRITE_EXECUTABLE  :: 4;
    }
    phase: Phase;
}

Message_Complete :: struct {
    #as using base: Message;
    error_code: s32;  // 0
}

// The workspace being compiled (ojay has just the one).
get_current_workspace :: () -> Workspace #compiler { return 1; }

compiler_begin_intercept :: (w: Workspace, flags: u32 = 0) #compiler {
    assert(w == 1, "compiler_begin_intercept: ojay has only the current workspace");
    __ojay_compiler_intercept(true);
}

compiler_end_intercept :: (w: Workspace) #compiler {
    __ojay_compiler_intercept(false);
}

// The next message (waiting while the compiler works). After .COMPLETE there
// are no more: it returns .COMPLETE again.
compiler_wait_for_message :: () -> *Message #compiler {
    return cast(*Message) __ojay_compiler_wait_for_message();
}

// Adds `source` (top-level declarations) to the program being compiled.
add_build_string :: (source: string, w: Workspace = 1, loc := #caller_location) #compiler {
    __ojay_add_build_string(source, loc);
}

// ------------------------------------------------------------------ Code <-> nodes

// The nodes of `code`: the root (an expression, or a Code_Block for #code {
// ... }), and all its nodes in order (each node before the nodes inside it).
compiler_get_nodes :: (code: Code) -> root: *Code_Node, expressions: [] *Code_Node #compiler {
    root := cast(*Code_Node) __ojay_code_to_nodes(code);
    all: [..] *Code_Node;
    collect_nodes(root, *all);
    return root, all;
}

// A Code value made of the nodes under `root` (which may be changed or newly
// made: allocate new nodes with New and set their kind). It means what the
// code it came from means where that was written.
compiler_get_code :: (root: *Code_Node) -> Code #compiler {
    return __ojay_nodes_to_code(root);
}

// The nodes directly inside `node`.
get_children :: (node: *Code_Node) -> [] *Code_Node #compiler {
    children: [..] *Code_Node;
    add :: (list: *[..] *Code_Node, n: *Code_Node) { if n  array_add(list, n); }
    if node.kind == {
        case .UNARY_OPERATOR;  add(*children, (cast(*Code_Unary_Operator) node).subexpression);
        case .BINARY_OPERATOR;
            b := cast(*Code_Binary_Operator) node;
            add(*children, b.left);
            add(*children, b.right);
        case .PROCEDURE_CALL;
            c := cast(*Code_Procedure_Call) node;
            add(*children, c.procedure_expression);
            for c.arguments_unsorted  add(*children, it.expression);
        case .CAST;
            c := cast(*Code_Cast) node;
            add(*children, c.target_type);
            add(*children, c.expression);
        case .ARRAY_SUBSCRIPT;
            s := cast(*Code_Array_Subscript) node;
            add(*children, s.array_or_pointer_expression);
            add(*children, s.subscript_expression);
        case .IF_EXPRESSION;
            x := cast(*Code_If_Expression) node;
            add(*children, x.condition);
            add(*children, x.then_expression);
            add(*children, x.else_expression);
        case .BLOCK;  for (cast(*Code_Block) node).statements  add(*children, it);
        case .DECLARATION;
            d := cast(*Code_Declaration) node;
            add(*children, d.type_inst);
            add(*children, d.expression);
        case .IF;
            i := cast(*Code_If) node;
            add(*children, i.condition);
            add(*children, i.then_block);
            add(*children, i.else_block);
        case .WHILE;
            w := cast(*Code_While) node;
            add(*children, w.condition);
            add(*children, w.block);
        case .FOR;
            f := cast(*Code_For) node;
            add(*children, f.iteration_expression);
            add(*children, f.iteration_expression_right);
            add(*children, f.block);
        case .RETURN;  for (cast(*Code_Return) node).arguments_unsorted  add(*children, it.expression);
        case .DEFER;   add(*children, (cast(*Code_Defer) node).statement);
    }
    return children;
}

#scope_file

collect_nodes :: (node: *Code_Node, all: *[..] *Code_Node) #compiler {
    if !node  return;
    array_add(all, node);
    for get_children(node)  collect_nodes(it, all);
}
