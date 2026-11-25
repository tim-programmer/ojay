// ERROR: Allocator_Mode, s64, s64, *void, *void) -> *void
// ERROR: (s64, *void) -> *void
my_allocator :: (size: s64, data: *void) -> *void { return null; }
main :: () {
    a := Allocator.{my_allocator, null};
}
