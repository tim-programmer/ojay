// A module with #scope_module and #scope_file declarations (tests/run/scope_directives.jai).
#load "part.jai";

api :: () -> int { return helper() + shared(); }

#scope_module
shared :: () -> int { return 100; }       // the module's files see it, importers do not

#scope_file
helper :: () -> int { return 1; }         // only this file sees it
counter := 5;

#scope_export
twice :: (x: int) -> int { return x * 2; }
twice :: (s: string) -> string { return join(s, s); }
#scope_module
twice :: (x: float) -> float { return x * 2; }   // not an overload importers see

#import "Basic";
