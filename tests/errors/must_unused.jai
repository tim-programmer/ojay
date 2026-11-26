// ERROR: the result of 'checked' must be used (it is marked #must)
checked :: (x: int) -> int #must { return x * 2; }
main :: () {
    checked(1);
}
