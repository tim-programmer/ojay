// File module on bare metal: the debugger's files, through semihosting
// (relative to the directory the debugger runs in).
// SKIP: x86_64
// SKIP: uart
#import "Basic";
#import "File";

main :: () {
    name := "ojay_file_semihosting_test.txt";
    print("%\n", write_entire_file(name, "hello, file\nsecond line\n"));
    print("%\n", file_exists(name));
    text, ok := read_entire_file(name);
    print("% [%]\n", ok, text);

    f, opened := file_open(name, for_writing = true, keep_existing_content = true);
    sought := file_seek(f, 0, .END);
    at_end, _ := file_tell(f);
    print("% %\n", sought, at_end);
    file_write(*f, "appended\n");
    length, _ := file_length(f);
    position, _ := file_tell(f);
    print("% % %\n", opened, length, position);
    file_seek(f, 7);
    file_seek(f, -3, .CURRENT);
    buffer: [8] u8;
    read_ok, n := file_read(f, buffer.data, 8);
    after, _ := file_tell(f);
    print("% % [%] %\n", read_ok, n, to_string(buffer.data, n), after);
    file_close(*f);

    // Reading past the end gives what is there.
    g, _ := file_open(name);
    file_seek(g, 30);
    read_ok, n = file_read(g, buffer.data, 8);
    print("% % [%]\n", read_ok, n, to_string(buffer.data, n));
    file_close(*g);

    moved := "ojay_file_semihosting_moved.txt";
    print("% % %\n", file_move(name, moved), file_exists(name), file_exists(moved));
    _, missing := read_entire_file("no_such_file.txt", log_errors = false);
    print("%\n", missing);
    print("% %\n", file_delete(moved), file_exists(moved));
    print("%\n", file_delete(moved));
}
