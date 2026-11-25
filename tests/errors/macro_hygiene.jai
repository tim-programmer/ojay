// ERROR: undeclared identifier 'secret'
// ERROR: in the expansion of macro 'peek' at
// ERROR: macro_hygiene.jai:8:10
// A macro body sees the names of its own scope, not the caller's (use `secret).
peek :: () -> int #expand { return secret; }
main :: () {
    secret := 1;
    x := peek();
}
