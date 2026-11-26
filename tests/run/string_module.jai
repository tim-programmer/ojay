#import "Basic";
#import "String";

main :: () {
    s := "  Hello, World  ";
    t := trim(s);
    print("[%] [%] [%]\n", t, trim_left(s), trim_right(s));
    print("% % % %\n", starts_with(t, "Hello"), ends_with(t, "World"), ends_with(t, #char "d"), begins_with(t, "x"));
    print("% % % %\n", find_index_from_left(t, "o"), find_index_from_right(t, #char "o"), contains(t, "lo, W"), contains(t, #char "z"));
    print("% %\n", count("a,b,,c", #char ","), count("abababa", "aba"));
    parts := split("a,b,,c", ",");
    print("% %\n", parts.count, parts);
    print("%\n", split("x y", #char " "));
    found, left, right := split_from_left("key=value=x", #char "=");
    print("% [%] [%]\n", found, left, right);
    found, left, right = split_from_right("key=value=x", "=");
    print("% [%] [%]\n", found, left, right);
    print("%\n", replace("the cat sat on the mat", "at", "og"));
    print("% %\n", to_upper_copy("MiXeD 1"), to_lower_copy("MiXeD 1"));
    print("% % %\n", equal_nocase("ABC", "abc"), compare_nocase("abc", "ABD"), compare_nocase("b", "A"));
    n, ok, rest := string_to_int("  -42 apples");
    print("% % [%]\n", n, ok, rest);
    n, ok, rest = string_to_int("ff", 16);
    print("% %\n", n, ok);
    n, ok, rest = string_to_int("x");
    print("% %\n", n, ok);
    f, fok, frest := string_to_float("-1.25e2xyz");
    print("% % [%]\n", f, fok, frest);
    p := "/home/user/file.tar.gz";
    ext, has := path_extension(p);
    print("% % % % %\n", path_strip_filename(p), path_filename(p), ext, has, path_strip_extension(p));
    x := "abc";
    advance(*x);
    print("% %\n", x, slice("hello", 1, 3));
}
