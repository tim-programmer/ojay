// ERROR: array index 4 is out of bounds (count is 4)
last :: (values: [4]int) -> int {
    i := values.count;
    return values[i];
}
X :: #run last(.[1, 2, 3, 4]);
