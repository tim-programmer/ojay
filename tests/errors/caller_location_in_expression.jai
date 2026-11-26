// ERROR: #caller_location is only allowed as a parameter's default value
f :: (line := #caller_location.line_number) {}
main :: () { f(); }
