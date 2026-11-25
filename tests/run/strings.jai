#import "Basic";

// Working with strings as byte arrays.

count_char :: (s: string, c: u8) -> int {
    n := 0;
    for i: 0..s.count-1 if s[i] == c n += 1;
    return n;
}

to_upper :: (s: string) -> string {
    out := s;
    out.data = cast(*u8) alloc(s.count);
    for i: 0..s.count-1 {
        c := s[i];
        if c >= #char "a" && c <= #char "z" c -= 32;
        out[i] = c;
    }
    return out;
}

reverse :: (s: string) -> string {
    out: string;
    out.count = s.count;
    out.data = cast(*u8) alloc(s.count);
    for i: 0..s.count-1 out[i] = s[s.count - 1 - i];
    return out;
}

is_palindrome :: (s: string) -> bool {
    i := 0;
    j := s.count - 1;
    while i < j {
        if s[i] != s[j] return false;
        i += 1;
        j -= 1;
    }
    return true;
}

word_count :: (s: string) -> int {
    words := 0;
    in_word := false;
    for i: 0..s.count-1 {
        space := s[i] == #char " " || s[i] == #char "\n";
        if !space && !in_word words += 1;
        in_word = !space;
    }
    return words;
}

TEXT :: #string END
The quick brown fox
jumps over the lazy dog
END

main :: () {
    s := "hello, world";
    print("'%' has % bytes, % 'l's, % 'o's\n", s, s.count, count_char(s, #char "l"), count_char(s, #char "o"));
    print("upper: %\n", to_upper(s));
    print("reversed: %\n", reverse(s));
    for word: string.["racecar", "level", "jai", "a", ""] {
        print("'%' palindrome: %\n", word, is_palindrome(word));
    }
    print("TEXT has % bytes and % words\n", TEXT.count, word_count(TEXT));
    print("first line ends at %\n", find_newline(TEXT));

    name := "Sailor";
    greeting := sprint("Hello, %! You are % years old.", name, 42);
    print("% (% bytes)\n", greeting, greeting.count);
    print("equal: % %\n", greeting == "Hello, Sailor! You are 42 years old.", greeting == name);

    // Substring by adjusting count/data
    sub := greeting;
    sub.data += 7;
    sub.count = 6;
    print("substring: '%'\n", sub);

    // Escapes and #char
    print("tab[\t] quote[\"] backslash[\\] hex[\x41] code=%\n", #char "A");
}

find_newline :: (s: string) -> int {
    for i: 0..s.count-1 if s[i] == #char "\n" return i;
    return -1;
}
