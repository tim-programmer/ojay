// ERROR: has #place or #align members, which are not supported by value across the C boundary
S :: struct {
    x: u8 #align 16;
}
take :: (s: S) #foreign;
main :: () { take(.{}); }
