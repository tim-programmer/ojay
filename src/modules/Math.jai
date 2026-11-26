// The part of module Math that is written in Jai. The rest (sqrt, sin, cos,
// floor, abs, min, max, PI, ...) is built into the compiler. Like the
// Preload, these declarations are only checked and compiled when a program
// uses them.

PI64  : float64 : 3.14159265358979323846;
TAU64 : float64 : 6.28318530717958647692;

clamp :: (x: $T, low: T, high: T) -> T {
    if x < low   return low;
    if x > high  return high;
    return x;
}

lerp :: (a: float32, b: float32, t: float32) -> float32 { return a + (b - a) * t; }

// ------------------------------------------------------------------ vectors

Vector2 :: struct {
    union {
        struct { x, y: float32; }
        component: [2] float32;
    }
}

Vector3 :: struct {
    union {
        struct { x, y, z: float32; }
        component: [3] float32;
    }
}

Vector4 :: struct {
    union {
        struct { x, y, z, w: float32; }
        component: [4] float32;
    }
}

make_vector2 :: (x: float32, y: float32) -> Vector2 {
    v: Vector2 = ---;
    v.x = x; v.y = y;
    return v;
}

make_vector3 :: (x: float32, y: float32, z: float32) -> Vector3 {
    v: Vector3 = ---;
    v.x = x; v.y = y; v.z = z;
    return v;
}

make_vector4 :: (x: float32, y: float32, z: float32, w: float32) -> Vector4 {
    v: Vector4 = ---;
    v.x = x; v.y = y; v.z = z; v.w = w;
    return v;
}

operator + :: (a: Vector2, b: Vector2) -> Vector2 { return make_vector2(a.x + b.x, a.y + b.y); }
operator - :: (a: Vector2, b: Vector2) -> Vector2 { return make_vector2(a.x - b.x, a.y - b.y); }
operator * :: (a: Vector2, b: Vector2) -> Vector2 { return make_vector2(a.x * b.x, a.y * b.y); }
operator / :: (a: Vector2, b: Vector2) -> Vector2 { return make_vector2(a.x / b.x, a.y / b.y); }
operator * :: (a: Vector2, k: float32) -> Vector2 #symmetric { return make_vector2(a.x * k, a.y * k); }
operator / :: (a: Vector2, k: float32) -> Vector2 { return make_vector2(a.x / k, a.y / k); }
operator - :: (a: Vector2) -> Vector2 { return make_vector2(-a.x, -a.y); }
operator == :: (a: Vector2, b: Vector2) -> bool { return a.x == b.x && a.y == b.y; }

operator + :: (a: Vector3, b: Vector3) -> Vector3 { return make_vector3(a.x + b.x, a.y + b.y, a.z + b.z); }
operator - :: (a: Vector3, b: Vector3) -> Vector3 { return make_vector3(a.x - b.x, a.y - b.y, a.z - b.z); }
operator * :: (a: Vector3, b: Vector3) -> Vector3 { return make_vector3(a.x * b.x, a.y * b.y, a.z * b.z); }
operator / :: (a: Vector3, b: Vector3) -> Vector3 { return make_vector3(a.x / b.x, a.y / b.y, a.z / b.z); }
operator * :: (a: Vector3, k: float32) -> Vector3 #symmetric { return make_vector3(a.x * k, a.y * k, a.z * k); }
operator / :: (a: Vector3, k: float32) -> Vector3 { return make_vector3(a.x / k, a.y / k, a.z / k); }
operator - :: (a: Vector3) -> Vector3 { return make_vector3(-a.x, -a.y, -a.z); }
operator == :: (a: Vector3, b: Vector3) -> bool { return a.x == b.x && a.y == b.y && a.z == b.z; }

operator + :: (a: Vector4, b: Vector4) -> Vector4 { return make_vector4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w); }
operator - :: (a: Vector4, b: Vector4) -> Vector4 { return make_vector4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w); }
operator * :: (a: Vector4, b: Vector4) -> Vector4 { return make_vector4(a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w); }
operator / :: (a: Vector4, b: Vector4) -> Vector4 { return make_vector4(a.x / b.x, a.y / b.y, a.z / b.z, a.w / b.w); }
operator * :: (a: Vector4, k: float32) -> Vector4 #symmetric { return make_vector4(a.x * k, a.y * k, a.z * k, a.w * k); }
operator / :: (a: Vector4, k: float32) -> Vector4 { return make_vector4(a.x / k, a.y / k, a.z / k, a.w / k); }
operator - :: (a: Vector4) -> Vector4 { return make_vector4(-a.x, -a.y, -a.z, -a.w); }
operator == :: (a: Vector4, b: Vector4) -> bool { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; }

dot :: (a: Vector2, b: Vector2) -> float32 { return a.x * b.x + a.y * b.y; }
dot :: (a: Vector3, b: Vector3) -> float32 { return a.x * b.x + a.y * b.y + a.z * b.z; }
dot :: (a: Vector4, b: Vector4) -> float32 { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

cross :: (a: Vector3, b: Vector3) -> Vector3 {
    return make_vector3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

length_squared :: (v: Vector2) -> float32 { return dot(v, v); }
length_squared :: (v: Vector3) -> float32 { return dot(v, v); }
length_squared :: (v: Vector4) -> float32 { return dot(v, v); }

length :: (v: Vector2) -> float32 { return sqrt(dot(v, v)); }
length :: (v: Vector3) -> float32 { return sqrt(dot(v, v)); }
length :: (v: Vector4) -> float32 { return sqrt(dot(v, v)); }

distance :: (a: Vector2, b: Vector2) -> float32 { return length(b - a); }
distance :: (a: Vector3, b: Vector3) -> float32 { return length(b - a); }
distance :: (a: Vector4, b: Vector4) -> float32 { return length(b - a); }

// `v` scaled to length 1; a vector shorter than `epsilon` comes back unchanged.
normalize :: (v: Vector2, epsilon: float32 = 0.000001) -> Vector2 {
    n := length(v);
    if n <= epsilon  return v;
    return v * (1.0 / n);
}
normalize :: (v: Vector3, epsilon: float32 = 0.000001) -> Vector3 {
    n := length(v);
    if n <= epsilon  return v;
    return v * (1.0 / n);
}
normalize :: (v: Vector4, epsilon: float32 = 0.000001) -> Vector4 {
    n := length(v);
    if n <= epsilon  return v;
    return v * (1.0 / n);
}

lerp :: (a: Vector2, b: Vector2, t: float32) -> Vector2 { return a + (b - a) * t; }
lerp :: (a: Vector3, b: Vector3, t: float32) -> Vector3 { return a + (b - a) * t; }
lerp :: (a: Vector4, b: Vector4, t: float32) -> Vector4 { return a + (b - a) * t; }
