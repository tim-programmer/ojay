// ERROR: #program_export procedures must be #c_call
S :: struct { a: s32; }
handler :: (s: S) -> S #program_export { return s; }
main :: () { handler(.{1}); }
