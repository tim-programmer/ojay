// ERROR: #run cannot use local variable 'n'
#import "Basic";
main :: () {
    n := 3;
    x := #run n * 2;
}
