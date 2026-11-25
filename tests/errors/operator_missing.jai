// ERROR: no 'operator +' for Vec2 and untyped integer
Vec2 :: struct { x, y: float; }
operator + :: (a: Vec2, b: Vec2) -> Vec2 { return .{a.x + b.x, a.y + b.y}; }
main :: () { v: Vec2; w := v + 1; }
