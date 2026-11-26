// ERROR: '..' passes an array to a variadic parameter, but this procedure has none
f :: (xs: []int) -> int { return xs.count; }
main :: () { a := int.[1, 2]; f(..a); }
