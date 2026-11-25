// ERROR: #modify of 'only_ints' rejected T = float32: only int and s32 are allowed
only_ints :: (x: $T) -> T #modify {
    return T == int || T == s32, "only int and s32 are allowed";
} {
    return x;
}
main :: () { only_ints(1.5); }
