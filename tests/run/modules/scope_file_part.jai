// #loaded by tests/run/scope_directives.jai.
from_part :: () -> int { return local_thing() + state; }

#scope_file
local_thing :: () -> int { return 7; }
state := 300;   // the main file has a private `state` too
