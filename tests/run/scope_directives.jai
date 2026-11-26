// #scope_export, #scope_module and #scope_file: declarations after
// #scope_module are private to their module, after #scope_file to their
// file; each file starts in #scope_export.
#import "Basic";
#import "Private";
P :: #import "Private";
#load "modules/scope_file_part.jai";

// The program's own names do not clash with the module's private ones.
helper :: () -> int { return 1000; }
shared :: (s: string) -> string { return s; }
twice :: (x: float) -> string { return "mine"; }

main :: () {
    print("% % % %\n", api(), part_api(), twice(4), P.twice(5));
    print("% %\n", P.twice("ab"), twice(1.5));
    print("% %\n", helper(), shared("program"));
    state += 1;
    print("% % % %\n", from_part(), state, picked, K);
}

#scope_file
state := 1;
LIMIT :: 3;
#if LIMIT == 3 { picked :: "three"; }
K :: #run LIMIT * 2;
