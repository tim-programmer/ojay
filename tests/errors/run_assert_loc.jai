// ERROR: assertion failed at
// ERROR: :5: must hold
#import "Basic";
check :: (x: int, loc := #caller_location) { assert(x > 0, "must hold", loc = loc); }
#run check(-1);
