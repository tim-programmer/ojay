// ERROR: '..' passes [2]s32 to a variadic parameter of type ..s64
f :: (xs: ..int) -> int { return xs.count; }
main :: () { a := s32.[1, 2]; f(..a); }
