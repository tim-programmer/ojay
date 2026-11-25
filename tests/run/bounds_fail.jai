// EXIT: 1
#import "Basic";
main :: () {
    a := int.[1, 2, 3];
    i := 5;
    print("before\n");
    a[i] = 0;
    print("unreachable\n");
}
