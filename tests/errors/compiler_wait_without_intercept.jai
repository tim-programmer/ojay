// ERROR: call compiler_begin_intercept(get_current_workspace()) first
#import "Compiler";
#run,stallable { m := compiler_wait_for_message(); }
main :: () {}
