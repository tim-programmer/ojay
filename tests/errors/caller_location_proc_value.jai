// ERROR: missing argument for parameter 1
// A procedure value has no default arguments: it must be given the location.
f :: (loc := #caller_location) {}
main :: () {
    p := f;
    p();
}
