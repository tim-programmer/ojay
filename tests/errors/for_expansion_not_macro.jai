// ERROR: for_expansion for R must be an #expand macro
R :: struct { n: int; }
for_expansion :: (r: *R, body: Code, flags: For_Flags) {}
main :: () { r := R.{3}; for r {} }
