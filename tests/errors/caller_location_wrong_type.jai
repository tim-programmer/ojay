// ERROR: #caller_location is a Source_Code_Location, but parameter 'loc' has type
f :: (x: int, loc: int = #caller_location) {}
main :: () { f(1); }
