// EXIT: 1
// assert's format arguments: the message is made (like tprint) only if the assertion fails.
#import "Basic";
calls := 0;
expensive :: () -> int { calls += 1; return 7; }
main :: () {
    x := 5;
    assert(x > 0, "x is % (%)", x, expensive());
    print("calls %\n", calls);
    S :: #run () -> int { assert(true, "never %", 1); return 1; }();
    assert(x > 10, "x is % but should be over %", x, 10);
    print("not reached\n");
}
