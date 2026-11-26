// ERROR: 'describe' needs '$T/Entity', but T = Rock: it is not Entity and has no 'using' member of that type
Entity :: struct { id: int; }
Rock :: struct { weight: float; }
describe :: (e: $T/Entity) {}
main :: () { r: Rock; describe(r); }
