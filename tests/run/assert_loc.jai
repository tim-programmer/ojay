// EXIT: 1
// assert with `loc`: the failure is reported where `check` was called.
#import "Basic";
check :: (x: int, loc := #caller_location) {
    assert(x > 0, loc = loc);
}
main :: () {
    check(1);
    print("ok so far\n");
    check(-1);
    print("not reached\n");
}
