// C side of c_abi_layout.jai: structs with #align members (_Alignas here) and
// #place overlaps (anonymous unions here) passed by value both ways. The
// layout is checked at compile time against what the Jai side prints.
#include <stddef.h>
#include <stdint.h>

typedef struct { uint8_t tag; _Alignas(16) uint8_t x; int32_t y; } Al16;
typedef struct { _Alignas(16) uint8_t x; } AlSmall;
typedef struct { float a; _Alignas(8) float b; } AlF;
typedef struct { _Alignas(16) double a; double b; } AlD;
typedef struct { union { int64_t a; struct { uint32_t lo, hi; }; }; } Pl2;
typedef struct { union { float a; float b; }; float c; } PlF;
typedef struct { union { double d; struct { float x, y; }; }; } PlD;
typedef struct { union { int8_t c; int32_t w; }; int16_t s; } PlB;

_Static_assert(sizeof(Al16) == 32 && _Alignof(Al16) == 16, "Al16");
_Static_assert(offsetof(Al16, x) == 16 && offsetof(Al16, y) == 20, "Al16 offsets");
_Static_assert(sizeof(AlSmall) == 16 && _Alignof(AlSmall) == 16, "AlSmall");
_Static_assert(sizeof(AlF) == 16 && offsetof(AlF, b) == 8, "AlF");
_Static_assert(sizeof(AlD) == 16 && offsetof(AlD, b) == 8, "AlD");
_Static_assert(sizeof(Pl2) == 8 && offsetof(Pl2, hi) == 4, "Pl2");
_Static_assert(sizeof(PlF) == 8 && offsetof(PlF, c) == 4, "PlF");
_Static_assert(sizeof(PlD) == 8 && offsetof(PlD, y) == 4, "PlD");
_Static_assert(sizeof(PlB) == 8 && offsetof(PlB, s) == 4, "PlB");

Al16 c_al16(Al16 v) { v.tag += 1; v.x += 2; v.y *= 3; return v; }
AlSmall c_alsmall(AlSmall v) { v.x += 1; return v; }
AlF c_alf(AlF v) { AlF r = {v.b, v.a}; return r; }
AlD c_ald(AlD v) { AlD r = {v.a + v.b, v.a - v.b}; return r; }
Pl2 c_pl2(Pl2 v) { uint32_t t = v.lo; v.lo = v.hi; v.hi = t; return v; }
PlF c_plf(PlF v) { PlF r; r.a = v.c; r.c = v.b * 2; return r; }
PlD c_pld(PlD v) { v.x += 1; v.y += 1; return v; }
PlB c_plb(PlB v) { v.w += 1; v.s -= 1; return v; }

// Calls back into Jai with the same types, after scalars that use up registers.
typedef Al16 (*Al16Fn)(int, int, int, Al16);
typedef PlD (*PlDFn)(double, PlD);
int32_t c_call_back(Al16Fn f, PlDFn g) {
  Al16 a = {1, 2, 3};
  Al16 r = f(10, 20, 30, a);
  PlD p;
  p.x = 1.5f;
  p.y = 2.5f;
  PlD q = g(0.5, p);
  return r.tag + r.x + r.y + (int32_t)(q.x * 10) + (int32_t)(q.y * 100);
}

// Over-aligned values on C's stack, handed to Jai by pointer.
int32_t c_aligned_local(int32_t (*f)(Al16 *)) {
  Al16 v = {7, 8, 9};
  return f(&v);
}
