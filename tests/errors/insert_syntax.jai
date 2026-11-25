// ERROR: <inserted at
// ERROR: insert_syntax.jai:9>:2:11: error: expected an expression, found ';'
// ERROR: in code inserted at
// A syntax error in a string inserted with #insert points into the inserted text.
make_code :: () -> string {
    return "x := 1;\ny := (2 + ;\n";
}
main :: () {
    #insert #run make_code();
}
