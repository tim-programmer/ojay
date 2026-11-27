// Module Program_Print: Code_Node trees back to source text, after Jai's
// module of the same name. Compile time only, like module Compiler.

#import "Basic";
#import "Compiler";

// Appends `node` as source text (an expression or a statement; statements in
// a block one per line, indented).
print_expression :: (builder: *String_Builder, node: *Code_Node) #compiler {
    print_node(builder, node, 0);
}

// `node` as source text (allocated with context.allocator).
expression_to_string :: (node: *Code_Node) -> string #compiler {
    b: String_Builder;
    print_node(*b, node, 0);
    return builder_to_string(*b);
}

// The text of an operator (Code_Unary_Operator / Code_Binary_Operator.operator_type).
operator_to_string :: (op: u32) -> string #compiler {
    if op < 0x100 {
        s := alloc_string(1);
        s[0] = cast(u8) op;
        return s;
    }
    if cast(Operator_Type) op == {
        case .ISEQUAL;         return "==";
        case .ISNOTEQUAL;      return "!=";
        case .LESS_EQUALS;     return "<=";
        case .GREATER_EQUALS;  return ">=";
        case .LOGICAL_AND;     return "&&";
        case .LOGICAL_OR;      return "||";
        case .SHIFT_LEFT;      return "<<";
        case .SHIFT_RIGHT;     return ">>";
        case .PLUS_EQUALS;     return "+=";
        case .MINUS_EQUALS;    return "-=";
        case .TIMES_EQUALS;    return "*=";
        case .DIVIDE_EQUALS;   return "/=";
        case .MOD_EQUALS;      return "%=";
        case .BITWISE_AND_EQUALS; return "&=";
        case .BITWISE_OR_EQUALS;  return "|=";
        case .BITWISE_XOR_EQUALS; return "^=";
        case .SHIFT_LEFT_EQUALS;  return "<<=";
        case .SHIFT_RIGHT_EQUALS; return ">>=";
        case .DEREFERENCE;     return ".*";
    }
    return "?";
}

#scope_file

indent :: (b: *String_Builder, depth: int) #compiler { for 1..depth  append(b, "    "); }

// How tightly a binary operator binds (higher: tighter), as in Jai.
precedence :: (op: u32) -> int #compiler {
    if op == #char "."  return 7;
    if op == #char "*" || op == #char "/" || op == #char "%" || op == #char "&" ||
       op == xx Operator_Type.SHIFT_LEFT || op == xx Operator_Type.SHIFT_RIGHT  return 5;
    if op == #char "+" || op == #char "-" || op == #char "|" || op == #char "^"  return 4;
    if op == #char "<" || op == #char ">" || op == xx Operator_Type.ISEQUAL || op == xx Operator_Type.ISNOTEQUAL ||
       op == xx Operator_Type.LESS_EQUALS || op == xx Operator_Type.GREATER_EQUALS  return 3;
    if op == xx Operator_Type.LOGICAL_AND  return 2;
    if op == xx Operator_Type.LOGICAL_OR  return 1;
    return 0;  // assignments
}

// An operand of an operator that binds `outer` tightly: in parentheses when
// it binds less tightly (or as tightly, on the right: `a - (b - c)`), and
// when it is an ifx.
operand :: (b: *String_Builder, node: *Code_Node, depth: int, outer := 6, right := false) #compiler {
    paren := false;
    if node && node.kind == .BINARY_OPERATOR {
        p := precedence((cast(*Code_Binary_Operator) node).operator_type);
        paren = p < outer || (right && p == outer);
    } else if node && node.kind == .IF_EXPRESSION {
        paren = true;
    }
    if paren  append(b, "(");
    print_node(b, node, depth);
    if paren  append(b, ")");
}

print_node :: (b: *String_Builder, node: *Code_Node, depth: int) #compiler {
    if !node  return;
    if node.kind == {
        case .LITERAL;
            l := cast(*Code_Literal) node;
            if l.value_type == {
                case .INTEGER; print_to_builder(b, "%", l._s64);
                case .FLOAT;   print_to_builder(b, "%", l._float64);
                case .BOOL;    append(b, ifx l._s64 then "true" else "false");
                case .POINTER; append(b, "null");
                case .STRING;
                    append(b, "\"");
                    for i: 0..l._string.count - 1 {
                        c := l._string[i];
                        if c == #char "\"" || c == #char "\\"  { append(b, "\\"); append(b, c); }
                        else if c == #char "\n"  append(b, "\\n");
                        else if c == #char "\t"  append(b, "\\t");
                        else  append(b, c);
                    }
                    append(b, "\"");
            }
        case .IDENT;  append(b, (cast(*Code_Ident) node).name);
        case .UNARY_OPERATOR;
            u := cast(*Code_Unary_Operator) node;
            if u.operator_type == xx Operator_Type.DEREFERENCE {
                operand(b, u.subexpression, depth);
                append(b, ".*");
            } else {
                append(b, operator_to_string(u.operator_type));
                operand(b, u.subexpression, depth);
            }
        case .BINARY_OPERATOR;
            o := cast(*Code_Binary_Operator) node;
            if o.operator_type == #char "." {
                if o.left  operand(b, o.left, depth);
                append(b, ".");
                print_node(b, o.right, depth);
            } else {
                p := precedence(o.operator_type);
                operand(b, o.left, depth, p);
                print_to_builder(b, " % ", operator_to_string(o.operator_type));
                operand(b, o.right, depth, p, right = true);
            }
        case .PROCEDURE_CALL;
            c := cast(*Code_Procedure_Call) node;
            print_node(b, c.procedure_expression, depth);
            append(b, "(");
            for c.arguments_unsorted {
                if it_index  append(b, ", ");
                if it.name  print_to_builder(b, "% = ", it.name.name);
                print_node(b, it.expression, depth);
            }
            append(b, ")");
        case .CAST;
            c := cast(*Code_Cast) node;
            if c.flags & .AUTO {
                append(b, "xx ");
            } else {
                append(b, "cast");
                if c.flags & .TRUNCATE  append(b, ",trunc");
                if c.flags & .NO_CHECK  append(b, ",no_check");
                append(b, "(");
                print_node(b, c.target_type, depth);
                append(b, ") ");
            }
            operand(b, c.expression, depth);
        case .ARRAY_SUBSCRIPT;
            s := cast(*Code_Array_Subscript) node;
            operand(b, s.array_or_pointer_expression, depth);
            append(b, "[");
            print_node(b, s.subscript_expression, depth);
            append(b, "]");
        case .IF_EXPRESSION;
            x := cast(*Code_If_Expression) node;
            append(b, "ifx ");
            print_node(b, x.condition, depth);
            append(b, " then ");
            print_node(b, x.then_expression, depth);
            append(b, " else ");
            print_node(b, x.else_expression, depth);
        case .BLOCK;
            append(b, "{\n");
            for (cast(*Code_Block) node).statements {
                indent(b, depth + 1);
                print_statement(b, it, depth + 1);
                append(b, "\n");
            }
            indent(b, depth);
            append(b, "}");
        case .DECLARATION;
            d := cast(*Code_Declaration) node;
            append(b, d.name);
            if d.type_inst {
                append(b, ": ");
                print_node(b, d.type_inst, depth);
                if d.expression || d.flags & .UNINITIALIZED  append(b, ifx d.flags & .IS_CONSTANT then " : " else " = ");
            } else {
                append(b, ifx d.flags & .IS_CONSTANT then " :: " else " := ");
            }
            if d.flags & .UNINITIALIZED  append(b, "---");
            else  print_node(b, d.expression, depth);
            append(b, ";");
        case .IF;
            i := cast(*Code_If) node;
            append(b, "if ");
            print_node(b, i.condition, depth);
            append(b, " ");
            print_statement(b, i.then_block, depth);
            if i.else_block {
                append(b, " else ");
                print_statement(b, i.else_block, depth);
            }
        case .WHILE;
            w := cast(*Code_While) node;
            append(b, "while ");
            print_node(b, w.condition, depth);
            append(b, " ");
            print_statement(b, w.block, depth);
        case .FOR;
            f := cast(*Code_For) node;
            append(b, "for ");
            if f.flags & .REVERSE  append(b, "< ");
            if f.flags & .POINTER  append(b, "* ");
            if f.index_name != "it_index"     print_to_builder(b, "%, %: ", f.ident_name, f.index_name);
            else if f.ident_name != "it"      print_to_builder(b, "%: ", f.ident_name);
            print_node(b, f.iteration_expression, depth);
            if f.iteration_expression_right {
                append(b, "..");
                print_node(b, f.iteration_expression_right, depth);
            }
            append(b, " ");
            print_statement(b, f.block, depth);
        case .RETURN;
            append(b, "return");
            for (cast(*Code_Return) node).arguments_unsorted {
                append(b, ifx it_index then ", " else " ");
                print_node(b, it.expression, depth);
            }
            append(b, ";");
        case .BREAK;     print_to_builder(b, "break%;", label_text((cast(*Code_Break_Continue) node).label));
        case .CONTINUE;  print_to_builder(b, "continue%;", label_text((cast(*Code_Break_Continue) node).label));
        case .DEFER;
            append(b, "defer ");
            print_statement(b, (cast(*Code_Defer) node).statement, depth);
        case;
            append(b, "<...>");  // syntax without a node type of its own
    }
}

// A statement: an expression or assignment gets its ';' (the other kinds
// print their own, or end with a block).
print_statement :: (b: *String_Builder, node: *Code_Node, depth: int) #compiler {
    print_node(b, node, depth);
    if node && (node.kind == .LITERAL || node.kind == .IDENT || node.kind == .UNARY_OPERATOR ||
                node.kind == .BINARY_OPERATOR || node.kind == .PROCEDURE_CALL || node.kind == .CAST ||
                node.kind == .ARRAY_SUBSCRIPT || node.kind == .IF_EXPRESSION)
        append(b, ";");
}

label_text :: (label: string) -> string #compiler { return ifx label.count then tprint(" %", label) else ""; }
