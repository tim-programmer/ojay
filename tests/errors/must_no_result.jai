// ERROR: #must is only allowed on a procedure that returns a value
f :: () #must {}
main :: () { f(); }
