// ERROR: assertion failed: table too small
#import "Basic";
check :: () -> bool {
    assert(size_of([4]u8) >= 8, "table too small");
    return true;
}
#run check();
