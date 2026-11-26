#import "Basic";

Color :: enum u8 { RED :: 3; GREEN :: 1; BLUE :: 7; }
Signed :: enum s16 { A :: -5; B :: 10; C :: 0; }
Big :: enum u64 { LOW :: 1; HIGH :: 0xFFFF_FFFF_FFFF_FFF0; }

GLOBAL :: 42;
B :: #import "Basic";

#if #exists(GLOBAL)  HAS_GLOBAL :: true;
else                 HAS_GLOBAL :: false;

#if #exists(NOT_THERE)  print_missing :: () { print("bad\n"); }
else                    print_missing :: () { print("NOT_THERE is missing\n"); }

describe :: (x: $T) #expand {
    #if is_constant(x)  print("% is constant\n", x);
    else                print("% is not constant\n", x);
}

where_am_i :: () -> string { return #procedure_name(); }

poly :: (x: $T) -> string { return #procedure_name(); }

main :: () {
    print("%\n", where_am_i());
    print("%\n", poly(1));
    print("% %\n", #procedure_name(), #procedure_name);

    print("% %\n", HAS_GLOBAL, #exists(main));
    print_missing();
    local := 5;
    print("% %\n", #exists(local), #exists(Basic));
    print("% %\n", #exists(B.print), #exists(B.nothing_here));

    n := 3;
    print("% % % %\n", is_constant(1 + 2), is_constant(n), is_constant(GLOBAL), is_constant("s"));
    describe(10);
    describe(n);

    print("%\n", enum_names(Color));
    print("%\n", enum_values_as_s64(Color));
    print("%\n", enum_values_as_enum(Color));
    lo, hi := enum_range(Color);
    print("% % % %\n", lo, hi, enum_lowest_value(Color), enum_highest_value(Color));
    print("% %\n", enum_lowest_value(Signed), enum_highest_value(Signed));
    print("% %\n", enum_lowest_value(Big), enum_highest_value(Big));
}
