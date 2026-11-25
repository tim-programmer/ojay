// ERROR: duplicate member 'x'
Vec2 :: struct { x, y: float; }
Entity :: struct { x: int; using pos: Vec2; }
main :: () { e: Entity; }
