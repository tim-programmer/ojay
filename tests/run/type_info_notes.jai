#import "Basic";

Config :: struct @Serializable @Version2 {
    name: string = "default" @Required;
    retries := 3 @Min1 @Max10;
    debug: bool;
}

Plain :: struct { a, b: int; }

describe :: (T: Type) {
    info := cast(*Type_Info_Struct) T;
    print("% notes: %\n", info.name, info.notes);
    for info.members  print("  %: %\n", it.name, it.notes);
    print("  initializer: %\n", info.initializer != null);
}

COMPILE_TIME :: #run () -> string {
    info := type_info(Config);
    c: Config = ---;
    memset(*c, 0, size_of(Config));
    info.initializer(*c);
    return tprint("% % % %", info.notes, info.members[1].notes, c.name, c.retries);
}();

main :: () {
    describe(Config);
    describe(Plain);
    c: Config;
    c.name = "changed";
    c.retries = 99;
    type_info(Config).initializer(*c);
    print("% %\n", c.name, c.retries);
    print("%\n", COMPILE_TIME);
}
