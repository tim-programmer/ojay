// File module (Linux: it calls the C library).
// SKIP: static
// SKIP: arm32
// SKIP: riscv
#import "Basic";
#import "File";

main :: () {
    dir := "/tmp";  // exists already: make_directory_if_it_does_not_exist is true
    print("%\n", make_directory_if_it_does_not_exist(dir));
    name := tprint("%/ojay_file_module_test.txt", dir);
    print("%\n", write_entire_file(name, "hello, file\nsecond line\n"));
    print("%\n", file_exists(name));
    text, ok := read_entire_file(name);
    print("% [%]\n", ok, text);

    f, opened := file_open(name, for_writing = true, keep_existing_content = true);
    file_seek(f, 0, .END);
    file_write(*f, "appended\n");
    length, _ := file_length(f);
    print("% %\n", opened, length);
    file_seek(f, 7);
    buffer: [4] u8;
    read_ok, n := file_read(f, buffer.data, 4);
    print("% % %\n", read_ok, n, to_string(buffer.data, n));
    file_close(*f);

    _, missing := read_entire_file("no/such/file", log_errors = false);
    print("%\n", missing);
    print("% %\n", file_delete(name), file_exists(name));
    print("%\n", file_delete(name));
}
