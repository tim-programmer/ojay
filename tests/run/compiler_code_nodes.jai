// Module Compiler: Code values as Code_Node trees (compiler_get_nodes),
// back to Code (compiler_get_code), and Program_Print's print_expression.
// COMPILE_OUTPUT: cast(float) -a.b[i] / 2.5
// COMPILE_OUTPUT: ifx p == null then "none" else xx p.*
// COMPILE_OUTPUT: (a - (b - c)) * (d + e)
// COMPILE_OUTPUT: {
// COMPILE_OUTPUT:     n: int = 3;
// COMPILE_OUTPUT:     for i: 0..n - 1 if i % 2 == 0 continue; else total += i;
// COMPILE_OUTPUT:     while n > 0 {
// COMPILE_OUTPUT:         n -= 1;
// COMPILE_OUTPUT:         defer done = true;
// COMPILE_OUTPUT:     }
// COMPILE_OUTPUT:     return n, "x\n";
// COMPILE_OUTPUT: }
// COMPILE_OUTPUT: built: 42
#import "Basic";
#import "Compiler";
#import "Program_Print";

check :: (condition: Code) #expand {
    #insert -> string {
        root := compiler_get_nodes(condition);
        text := expression_to_string(root);
        return tprint("if !(#insert condition) print(\"check failed: %\\n\"); else print(\"ok: %\\n\");", text, text);
    }
}

// Every + in the code becomes *.
times :: (c: Code) -> Code #compiler {
    root, all := compiler_get_nodes(c);
    for all {
        if it.kind == .BINARY_OPERATOR {
            b := cast(*Code_Binary_Operator) it;
            if b.operator_type == #char "+"  b.operator_type = #char "*";
        }
    }
    return compiler_get_code(root);
}

count_nodes :: (c: Code) -> int #compiler {
    _, all := compiler_get_nodes(c);
    return all.count;
}

main :: () {
    x := 3;
    y := 4;
    check(x + y == 7);
    check(x * y > 20 && foo(x, scale = 2) != 0);
    z := #insert #run times(#code (x + y) + 1);
    print("z = %\n", z);
    print("% nodes\n", #run count_nodes(#code { a := 1; if a > 0 then a += 2; }));
}
foo :: (v: int, scale := 1) -> int { return v * scale; }

// More node kinds, printed back as source text.
shown :: (c: Code) -> string #compiler {
    root := compiler_get_nodes(c);
    return expression_to_string(root);
}
print_shown :: () #compiler {
    print("%\n", shown(#code cast(float) -a.b[i] / 2.5));
    print("%\n", shown(#code ifx p == null then "none" else xx p.*));
    print("%\n", shown(#code (a - (b - c)) * (d + e)));
    print("%\n", shown(#code {
        n: int = 3;
        for i: 0..n-1  if i % 2 == 0  continue; else total += i;
        while n > 0 { n -= 1; defer done = true; }
        return n, "x\n";
    }));
}
#run print_shown();

// Nodes built from scratch: `answer * 2`, inserted as an expression.
doubled :: (name: string) -> Code #compiler {
    id := New(Code_Ident);
    id.kind = .IDENT;
    id.name = name;
    two := New(Code_Literal);
    two.kind = .LITERAL;
    two.value_type = .INTEGER;
    two._s64 = 2;
    mul := New(Code_Binary_Operator);
    mul.kind = .BINARY_OPERATOR;
    mul.operator_type = #char "*";
    mul.left = id;
    mul.right = two;
    return compiler_get_code(mul);
}
answer :: 21;
#run print("built: %\n", #insert #run doubled("answer"));
