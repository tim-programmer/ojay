// ERROR: #expand macro 'forever' is expanded more than 100 levels deep; does it expand itself without end?
// ERROR: the outermost expansion is 'forever' at
// ERROR: macro_recursion.jai:6:14
forever :: (n: int) #expand { forever(n + 1); }

main :: () { forever(0); }
