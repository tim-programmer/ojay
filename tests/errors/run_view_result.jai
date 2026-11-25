// ERROR: return a fixed-size array ([N]s64) instead
#import "Basic";
make :: () -> []int {
    a: [..]int;
    array_add(*a, 1);
    return a;
}
X :: #run make();
