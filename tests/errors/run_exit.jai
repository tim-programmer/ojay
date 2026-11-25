// ERROR: exit(2) was called during compile-time execution
#import "Basic";
setup :: () { exit(2); }
#run setup();
