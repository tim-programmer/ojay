// ERROR: '`counter': 'counter' is not declared where macro 'bump' is called
// ERROR: macro_backtick_undeclared.jai:7:5
bump :: () #expand { `counter += 1; }

main :: () {
    count := 0;
    bump();
}
