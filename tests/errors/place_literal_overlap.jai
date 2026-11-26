// ERROR: members 'a' and 'lo' of S overlap (#place); a struct literal can only set one of them
S :: struct {
    a: s64;
    #place a;
    lo: u32;
    hi: u32;
}
main :: () { s := S.{a = 1, lo = 2}; }
