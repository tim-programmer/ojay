#import "Basic";

helper :: () -> string { return "Util.helper"; }
describe :: (x: int) -> string { return sprint("int %", x); }
shout :: (s: string) { print("%!\n", s); }
