// ERROR: 'move' needs '$T/interface Has_Position', but T = Bad: its member 'y' is s64, not float32
Has_Position :: struct { x: float; y: float; }
Bad :: struct { x: float; y: int; }
move :: (p: $T/interface Has_Position) {}
main :: () { b: Bad; move(b); }
