// ERROR: only a top-level #run,stallable can intercept the compilation
#import "Compiler";
#run { compiler_begin_intercept(get_current_workspace()); }
main :: () {}
