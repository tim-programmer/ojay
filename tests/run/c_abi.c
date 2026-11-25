// C side of c_abi.jai: receives structs from Jai and calls Jai back.
// Compiled with the target's C compiler (cc, arm-none-eabi-gcc,
// riscv32-esp-elf-gcc), so it also checks ojay against GCC's lowering.
#include <stdbool.h>
#include <stdint.h>

typedef struct { int8_t a; } S1;
typedef struct { int16_t a; } S2;
typedef struct { int8_t a, b, c; } S3;
typedef struct { int32_t a; } S4;
typedef struct { int32_t a, b; } S8;
typedef struct { int32_t a, b, c; } S12;
typedef struct { int32_t a, b, c, d; } S16;
typedef struct { int64_t a, b, c; } S24;
typedef struct { int32_t a[16]; } S64;
typedef struct { int32_t a[18]; } S72;
typedef struct { float f; int32_t i; } FI;
typedef struct { double a, b; } DD;
typedef struct { float a; } F1;
typedef struct { float a, b; } F2;
typedef struct { float a, b, c; } F3;
typedef struct { float a, b, c, d; } F4;
typedef struct { float a, b, c, d, e; } F5;
typedef struct { double a, b, c, d; } D4;
typedef struct { int8_t a; double d; } ID;
typedef struct { bool flag; uint8_t color; uint16_t small; int32_t *p; } Mixed;
typedef struct { int16_t x, y; } Inner;
typedef struct { uint8_t name[3]; Inner inner; Inner pts[2]; float f[2]; } Nested;
typedef union { float f; int32_t i; } UFI;
typedef struct {} Empty;  // GNU C: size 0, takes no argument slot

S1 bump_s1(S1 x) { x.a++; return x; }
S2 bump_s2(S2 x) { x.a++; return x; }
S3 bump_s3(S3 x) { x.a++; x.b++; x.c++; return x; }
S4 bump_s4(S4 x) { x.a++; return x; }
S8 bump_s8(S8 x) { x.a++; x.b++; return x; }
S12 bump_s12(S12 x) { x.a++; x.b++; x.c++; return x; }
S16 bump_s16(S16 x) { x.a++; x.b++; x.c++; x.d++; return x; }
S24 bump_s24(S24 x) { x.a++; x.b++; x.c++; return x; }
S64 bump_s64(S64 x) { for (int i = 0; i < 16; i++) x.a[i]++; return x; }
S72 bump_s72(S72 x) { for (int i = 0; i < 18; i++) x.a[i]++; return x; }
FI bump_fi(FI x) { x.f += 0.5f; x.i++; return x; }
DD bump_dd(DD x) { x.a += 0.5; x.b += 0.5; return x; }
F1 bump_f1(F1 x) { x.a += 0.5f; return x; }
F2 bump_f2(F2 x) { x.a += 0.5f; x.b += 0.5f; return x; }
F3 bump_f3(F3 x) { x.a += 0.5f; x.b += 0.5f; x.c += 0.5f; return x; }
F4 bump_f4(F4 x) { x.a += 0.5f; x.b += 0.5f; x.c += 0.5f; x.d += 0.5f; return x; }
F5 bump_f5(F5 x) { x.a += 0.5f; x.b += 0.5f; x.c += 0.5f; x.d += 0.5f; x.e += 0.5f; return x; }
D4 bump_d4(D4 x) { x.a += 0.5; x.b += 0.5; x.c += 0.5; x.d += 0.5; return x; }
ID bump_id(ID x) { x.a++; x.d += 0.5; return x; }
Mixed bump_mixed(Mixed x) { x.flag = !x.flag; x.color++; x.small++; (*x.p)++; return x; }
Nested bump_nested(Nested x) {
  for (int i = 0; i < 3; i++) x.name[i]++;
  x.inner.x++; x.inner.y++;
  for (int i = 0; i < 2; i++) { x.pts[i].x++; x.pts[i].y++; x.f[i] += 0.5f; }
  return x;
}
UFI bump_ufi(UFI x) { x.i++; return x; }

int64_t spill_s16(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f, S16 x, int32_t g) {
  return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * x.a + 8 * x.b + 9 * x.c + 10 * x.d + 11 * g;
}
int64_t spill_s8x2(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f, S8 x, S8 y) {
  return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * x.a + 8 * x.b + 9 * y.a + 10 * y.b;
}
int64_t split_s12(int32_t a, int32_t b, int32_t c, S12 x, S3 y) {
  return a + 2 * b + 3 * c + 4 * x.a + 5 * x.b + 6 * x.c + 7 * y.a + 8 * y.b + 9 * y.c;
}
double spill_dd(double a, double b, double c, double d, double e, double f, double g, double h, DD x, F3 y,
                int32_t k) {
  return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h + 9 * x.a + 10 * x.b + 11 * y.a + 12 * y.b +
         13 * y.c + 14 * k;
}
float spill_f4(F4 a, F4 b, F4 c, F4 d, F4 e, float f) {
  return a.a + b.b + c.c + d.d + 10 * e.a + 100 * e.d + 1000 * f;
}
S24 spill_sret(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, S16 x, ID y) {
  S24 r = {a + b + c + d + e, x.a * 1000 + x.b * 100 + x.c * 10 + x.d, (int64_t)(y.a * y.d)};
  return r;
}
double mixed_args(int8_t a, S3 b, float c, DD d, uint16_t e, ID f, bool g, F5 h) {
  return a + 2 * (b.a + b.b + b.c) + 3 * c + 4 * (d.a + d.b) + 5 * e + 6 * (f.a + f.d) + 7 * g +
         8 * (h.a + h.b + h.c + h.d + h.e);
}
static int32_t empty_calls;
Empty with_empty(Empty a, int32_t x, Empty b) { empty_calls += x; Empty r; return r; }
int32_t empty_count(void) { return empty_calls; }

S3 call_s3(S3 (*cb)(S3, int32_t), S3 x) { return cb(x, 3); }
S12 call_s12(S12 (*cb)(S12, int32_t), S12 x) { return cb(x, 3); }
S24 call_s24(S24 (*cb)(S24, int32_t), S24 x) { return cb(x, 3); }
S72 call_s72(S72 (*cb)(S72, int32_t), S72 x) { return cb(x, 3); }
DD call_dd(DD (*cb)(DD, int32_t), DD x) { return cb(x, 3); }
F3 call_f3(F3 (*cb)(F3, int32_t), F3 x) { return cb(x, 3); }
F5 call_f5(F5 (*cb)(F5, int32_t), F5 x) { return cb(x, 3); }
ID call_id(ID (*cb)(ID, int32_t), ID x) { return cb(x, 3); }
Nested call_nested(Nested (*cb)(Nested, int32_t), Nested x) { return cb(x, 3); }
UFI call_ufi(UFI (*cb)(UFI, int32_t), UFI x) { return cb(x, 3); }
int64_t call_spill(int64_t (*cb)(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, S16, DD, F4)) {
  S16 x = {7, 8, 9, 10};
  DD y = {11.5, 12.5};
  F4 z = {1, 2, 3, 4};
  return cb(1, 2, 3, 4, 5, 6, x, y, z);
}
int32_t call_empty(Empty (*cb)(Empty, int32_t, Empty), int32_t x) {
  Empty e;
  cb(e, x, e);
  return x * 2;
}

S24 jai_combine(S12 a, DD b, S3 c);
S1 jai_tiny_renamed(S1 x, F1 y);
S24 c_calls_exported(void) {
  S12 a = {100, 200, 300};
  DD b = {0.5, 1.5};
  S3 c = {-1, -2, -3};
  S24 r = jai_combine(a, b, c);
  S1 x = {20};
  F1 y = {22};
  r.c += jai_tiny_renamed(x, y).a * 1000;
  return r;
}
