// ERROR: '$T' is introduced twice in 'pair'; write later uses as plain 'T'
pair :: (a: $T, b: $T) {}
main :: () { pair(1, 2); }
