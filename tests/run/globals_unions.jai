#import "Basic";

// Global state, unions, compile-time conditions and constants.

DEBUG :: false;
VERSION :: 3;
BUFFER_SIZE :: VERSION * 16;

#assert BUFFER_SIZE == 48;

Config :: struct {
    name := "default";
    retries := 3;
    ratio: float = 0.5;
}

config: Config;
counter := 100;
table := int.[1, 1, 2, 3, 5, 8];
buffer: [BUFFER_SIZE]u8;

Bits :: union {
    f: float32;
    u: u32;
    bytes: [4]u8;
}

next_id :: () -> int {
    counter += 1;
    return counter;
}

main :: () {
    print("config = %\n", config);
    config.retries = 5;
    config.name = "custom";
    print("config = %\n", config);

    print("ids: % % %\n", next_id(), next_id(), next_id());
    sum := 0;
    for table sum += it;
    print("table sum = %, buffer size = %, first byte = %\n", sum, buffer.count, buffer[0]);
    buffer[BUFFER_SIZE - 1] = 255;
    print("last byte = %\n", buffer[BUFFER_SIZE - 1]);

    b: Bits;
    b.f = 1.0;
    print("1.0 as bits: % (0x3f800000 = %)\n", b.u, 0x3f80_0000);
    print("bytes (little endian): %\n", b.bytes);
    b.u = 0xC0490FDB;
    print("0xC0490FDB as float32: %\n", b.f);
    print("size_of(Bits) = %\n", size_of(Bits));

    #if DEBUG {
        print("debug build\n");
    } else {
        print("release build, version %\n", VERSION);
    }

    #if VERSION >= 3 {
        print("version 3 features enabled\n");
    }
}
