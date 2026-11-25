// ERROR: type constraints ($T/Interface) are not supported yet
Shape :: struct { sides: int; }
sides :: (s: $T/Shape) -> int { return s.sides; }
