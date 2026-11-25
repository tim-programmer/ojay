// ERROR: procedure values in #run results are not supported yet
// A Context holds procedure values (its allocator's and logger's), so it cannot be a #run result.
current :: () -> Context { return context; }
X :: #run current();
main :: () {}
