// Module Random: pseudo-random numbers (xorshift128+), after Jai's module of
// the same name. The state is in the context (context.random_state), so each
// context has its own sequence; random_seed starts a new one.

Random_State :: struct {
    low:  u64 = 0x4c6a_8c3f_9b2e_17d5;
    high: u64 = 0x9e37_79b9_7f4a_7c15;
}

#add_context random_state: Random_State;

// Starts the sequence for `seed` (any value; the same seed gives the same numbers).
random_seed :: (seed: u64) {
    s := seed;
    context.random_state.low = splitmix64(*s);
    context.random_state.high = splitmix64(*s);
}

// 64 random bits.
random_get :: () -> u64 {
    state := *context.random_state;
    s1 := state.low;
    s0 := state.high;
    state.low = s0;
    s1 ^= s1 << 23;
    state.high = s1 ^ s0 ^ (s1 >> 17) ^ (s0 >> 26);
    return state.high + s0;
}

// In [0, 1].
random_get_zero_to_one :: () -> float {
    return cast(float) (random_get() >> 40) / cast(float) 0xFF_FFFF;
}

// In [0, 1).
random_get_zero_to_one_open :: () -> float {
    return cast(float) (random_get() >> 40) / cast(float) 0x100_0000;
}

// In [0, 1), with 53 bits.
random_get_zero_to_one_float64 :: () -> float64 {
    return cast(float64) (random_get() >> 11) / cast(float64) 0x20_0000_0000_0000;
}

// In [min, max].
random_get_within_range :: (min: float, max: float) -> float {
    return min + (max - min) * random_get_zero_to_one();
}

// An integer in [min, max] (both included).
random_get_within_range :: (min: s64, max: s64) -> s64 {
    if max <= min  return min;
    span := cast,no_check(u64) (max - min) + 1;
    if span == 0  return cast,no_check(s64) random_get();  // the whole range of s64
    return min + cast,no_check(s64) (random_get() % span);
}

#scope_file

splitmix64 :: (s: *u64) -> u64 {
    s.* += 0x9e37_79b9_7f4a_7c15;
    z := s.*;
    z = (z ^ (z >> 30)) * 0xbf58_476d_1ce4_e5b9;
    z = (z ^ (z >> 27)) * 0x94d0_49bb_1331_11eb;
    return z ^ (z >> 31);
}
