// ERROR: `break: macro 'leave' is not called inside a loop
leave :: () #expand { `break; }
main :: () { leave(); }
