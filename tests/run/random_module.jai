#import "Basic";
#import "Random";

main :: () {
    random_seed(1234);
    a := random_get();
    b := random_get();
    random_seed(1234);
    print("% %\n", a == random_get(), b == random_get());
    lo, hi := 100, -100;
    in_range := true;
    total: float64;
    for 1..10000 {
        x := random_get_within_range(-3, 7);
        if x < lo lo = x;
        if x > hi hi = x;
        f := random_get_zero_to_one();
        if f < 0 || f > 1 in_range = false;
        g := random_get_within_range(2.0, 4.0);
        if g < 2 || g > 4 in_range = false;
        total += random_get_zero_to_one_float64();
    }
    print("% % % %\n", lo, hi, in_range, total > 4800 && total < 5200);
    X :: #run () -> u64 { random_seed(1234); return random_get(); }();
    print("%\n", X == a);
}
