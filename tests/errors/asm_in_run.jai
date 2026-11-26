// ERROR: #asm cannot run during compile-time execution
f :: () -> int { x := 1; #asm { inc x } return x; }
X :: #run f();
main :: () {}
