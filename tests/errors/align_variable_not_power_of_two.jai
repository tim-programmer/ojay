// ERROR: #align 24: the alignment must be a power of two
main :: () {
    buffer: [64] u8 #align 24;
}
