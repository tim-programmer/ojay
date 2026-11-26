// C side of distinct_types.jai: distinct types cross the C boundary as their
// base types.
#include <stdint.h>

typedef struct { float x, y; } Vec2;
typedef struct { int32_t a; double d; } Pair;

uint32_t next_handle(uint32_t h) { return h + 1; }
Vec2 swap_point(Vec2 p) { Vec2 r = {p.y, p.x}; return r; }
double pair_sum(Pair p) { return p.a + p.d; }
int32_t apply_twice(int32_t (*f)(int32_t), int32_t x) { return f(f(x)); }
