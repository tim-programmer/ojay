// ERROR: division by zero
// ERROR: in 'average', called at
// ERROR: in 'summarize', called at
// ERROR: during compile-time execution (#run at
average :: (values: []int) -> int {
    total := 0;
    for values  total += it;
    return total / values.count;
}
summarize :: () -> int {
    empty: [..]int;
    return average(empty);
}
AVERAGE :: #run summarize();
