// ERROR: did you forget #import "Hardware"
main :: () { x := volatile_load(cast(*u32) 0x1000); }
