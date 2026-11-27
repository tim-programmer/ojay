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

// ------------------------------------------------------------------ matrices
//
// Row-major, as in Jai: `_23` is row 2, column 3, and a matrix transforms
// column vectors (`m * v`). `coef[row][column]` and `v[row]` (Matrix4: the
// rows as Vector4s) view the same numbers.

Matrix2 :: struct {
    union {
        struct {
            _11, _12: float32;
            _21, _22: float32;
        }
        coef: [2][2] float32;
        floats: [4] float32;
    }
}

Matrix3 :: struct {
    union {
        struct {
            _11, _12, _13: float32;
            _21, _22, _23: float32;
            _31, _32, _33: float32;
        }
        coef: [3][3] float32;
        floats: [9] float32;
    }
}

Matrix4 :: struct {
    union {
        struct {
            _11, _12, _13, _14: float32;
            _21, _22, _23, _24: float32;
            _31, _32, _33, _34: float32;
            _41, _42, _43, _44: float32;
        }
        coef: [4][4] float32;
        v: [4] Vector4;
        floats: [16] float32;
    }
}

matrix2_identity :: () -> Matrix2 {
    m: Matrix2;
    m._11 = 1; m._22 = 1;
    return m;
}
matrix3_identity :: () -> Matrix3 {
    m: Matrix3;
    m._11 = 1; m._22 = 1; m._33 = 1;
    return m;
}
matrix4_identity :: () -> Matrix4 {
    m: Matrix4;
    m._11 = 1; m._22 = 1; m._33 = 1; m._44 = 1;
    return m;
}

Matrix2_Identity :: #run matrix2_identity();
Matrix3_Identity :: #run matrix3_identity();
Matrix4_Identity :: #run matrix4_identity();

operator * :: (a: Matrix2, b: Matrix2) -> Matrix2 {
    r: Matrix2;
    for i: 0..1  for j: 0..1  for k: 0..1  r.coef[i][j] += a.coef[i][k] * b.coef[k][j];
    return r;
}
operator * :: (a: Matrix3, b: Matrix3) -> Matrix3 {
    r: Matrix3;
    for i: 0..2  for j: 0..2  for k: 0..2  r.coef[i][j] += a.coef[i][k] * b.coef[k][j];
    return r;
}
operator * :: (a: Matrix4, b: Matrix4) -> Matrix4 {
    r: Matrix4;
    for i: 0..3  for j: 0..3  for k: 0..3  r.coef[i][j] += a.coef[i][k] * b.coef[k][j];
    return r;
}
operator * :: (m: Matrix2, v: Vector2) -> Vector2 { return multiply(m, v); }
operator * :: (m: Matrix3, v: Vector3) -> Vector3 { return multiply(m, v); }
operator * :: (m: Matrix4, v: Vector4) -> Vector4 { return multiply(m, v); }
operator * :: (m: Matrix2, k: float32) -> Matrix2 #symmetric { r := m; for * r.floats  it.* *= k; return r; }
operator * :: (m: Matrix3, k: float32) -> Matrix3 #symmetric { r := m; for * r.floats  it.* *= k; return r; }
operator * :: (m: Matrix4, k: float32) -> Matrix4 #symmetric { r := m; for * r.floats  it.* *= k; return r; }
operator + :: (a: Matrix2, b: Matrix2) -> Matrix2 { r := a; for 0..3  r.floats[it] += b.floats[it]; return r; }
operator + :: (a: Matrix3, b: Matrix3) -> Matrix3 { r := a; for 0..8  r.floats[it] += b.floats[it]; return r; }
operator + :: (a: Matrix4, b: Matrix4) -> Matrix4 { r := a; for 0..15  r.floats[it] += b.floats[it]; return r; }
operator - :: (a: Matrix2, b: Matrix2) -> Matrix2 { r := a; for 0..3  r.floats[it] -= b.floats[it]; return r; }
operator - :: (a: Matrix3, b: Matrix3) -> Matrix3 { r := a; for 0..8  r.floats[it] -= b.floats[it]; return r; }
operator - :: (a: Matrix4, b: Matrix4) -> Matrix4 { r := a; for 0..15  r.floats[it] -= b.floats[it]; return r; }
operator == :: (a: Matrix2, b: Matrix2) -> bool { for 0..3  if a.floats[it] != b.floats[it]  return false; return true; }
operator == :: (a: Matrix3, b: Matrix3) -> bool { for 0..8  if a.floats[it] != b.floats[it]  return false; return true; }
operator == :: (a: Matrix4, b: Matrix4) -> bool { for 0..15  if a.floats[it] != b.floats[it]  return false; return true; }

multiply :: (m: Matrix2, v: Vector2) -> Vector2 {
    return make_vector2(m._11 * v.x + m._12 * v.y, m._21 * v.x + m._22 * v.y);
}
multiply :: (m: Matrix3, v: Vector3) -> Vector3 {
    return make_vector3(m._11 * v.x + m._12 * v.y + m._13 * v.z,
                        m._21 * v.x + m._22 * v.y + m._23 * v.z,
                        m._31 * v.x + m._32 * v.y + m._33 * v.z);
}
multiply :: (m: Matrix4, v: Vector4) -> Vector4 {
    return make_vector4(dot(m.v[0], v), dot(m.v[1], v), dot(m.v[2], v), dot(m.v[3], v));
}

// A point (w = 1: translated) and a direction (w = 0: not translated).
transform_point :: (m: Matrix4, p: Vector3) -> Vector3 {
    r := multiply(m, make_vector4(p.x, p.y, p.z, 1));
    if r.w != 0 && r.w != 1  return make_vector3(r.x / r.w, r.y / r.w, r.z / r.w);
    return make_vector3(r.x, r.y, r.z);
}
transform_vector :: (m: Matrix4, d: Vector3) -> Vector3 {
    r := multiply(m, make_vector4(d.x, d.y, d.z, 0));
    return make_vector3(r.x, r.y, r.z);
}

transpose :: (m: Matrix2) -> Matrix2 {
    r := m;
    r._12 = m._21;
    r._21 = m._12;
    return r;
}
transpose :: (m: Matrix3) -> Matrix3 {
    r: Matrix3 = ---;
    for i: 0..2  for j: 0..2  r.coef[i][j] = m.coef[j][i];
    return r;
}
transpose :: (m: Matrix4) -> Matrix4 {
    r: Matrix4 = ---;
    for i: 0..3  for j: 0..3  r.coef[i][j] = m.coef[j][i];
    return r;
}

determinant :: (m: Matrix2) -> float32 { return m._11 * m._22 - m._12 * m._21; }
determinant :: (m: Matrix3) -> float32 {
    return m._11 * (m._22 * m._33 - m._23 * m._32)
         - m._12 * (m._21 * m._33 - m._23 * m._31)
         + m._13 * (m._21 * m._32 - m._22 * m._31);
}
determinant :: (m: Matrix4) -> float32 {
    c := cofactors(m);
    return m._11 * c[0] + m._12 * c[1] + m._13 * c[2] + m._14 * c[3];
}

// The inverse, and false (and the identity) if m has none.
inverse :: (m: Matrix2) -> Matrix2, bool {
    d := determinant(m);
    if d == 0  return Matrix2_Identity, false;
    r: Matrix2 = ---;
    r._11 = m._22 / d;
    r._12 = -m._12 / d;
    r._21 = -m._21 / d;
    r._22 = m._11 / d;
    return r, true;
}
inverse :: (m: Matrix3) -> Matrix3, bool {
    d := determinant(m);
    if d == 0  return Matrix3_Identity, false;
    r: Matrix3 = ---;
    r._11 = (m._22 * m._33 - m._23 * m._32) / d;
    r._12 = (m._13 * m._32 - m._12 * m._33) / d;
    r._13 = (m._12 * m._23 - m._13 * m._22) / d;
    r._21 = (m._23 * m._31 - m._21 * m._33) / d;
    r._22 = (m._11 * m._33 - m._13 * m._31) / d;
    r._23 = (m._13 * m._21 - m._11 * m._23) / d;
    r._31 = (m._21 * m._32 - m._22 * m._31) / d;
    r._32 = (m._12 * m._31 - m._11 * m._32) / d;
    r._33 = (m._11 * m._22 - m._12 * m._21) / d;
    return r, true;
}
inverse :: (m: Matrix4) -> Matrix4, bool {
    // The adjugate (transposed cofactors) over the determinant.
    adj: Matrix4 = ---;
    for i: 0..3  for j: 0..3  adj.coef[j][i] = cofactor(m, i, j);
    d := m._11 * adj._11 + m._12 * adj._21 + m._13 * adj._31 + m._14 * adj._41;
    if d == 0  return Matrix4_Identity, false;
    return adj * (1 / d), true;
}

make_translation_matrix4 :: (t: Vector3) -> Matrix4 {
    m := Matrix4_Identity;
    m._14 = t.x; m._24 = t.y; m._34 = t.z;
    return m;
}
make_scale_matrix4 :: (s: Vector3) -> Matrix4 {
    m := Matrix4_Identity;
    m._11 = s.x; m._22 = s.y; m._33 = s.z;
    return m;
}
make_scale_matrix2 :: (s: Vector2) -> Matrix2 {
    m := Matrix2_Identity;
    m._11 = s.x; m._22 = s.y;
    return m;
}
// A rotation by `angle` radians (counterclockwise: x towards y).
make_rotation_matrix2 :: (angle: float32) -> Matrix2 {
    c := cos(angle);
    s := sin(angle);
    m: Matrix2 = ---;
    m._11 = c; m._12 = -s;
    m._21 = s; m._22 = c;
    return m;
}
make_scale_matrix3 :: (s: Vector3) -> Matrix3 {
    m := Matrix3_Identity;
    m._11 = s.x; m._22 = s.y; m._33 = s.z;
    return m;
}

// The upper-left 3x3 part, and a Matrix4 around a Matrix3.
make_matrix3 :: (m: Matrix4) -> Matrix3 {
    r: Matrix3 = ---;
    for i: 0..2  for j: 0..2  r.coef[i][j] = m.coef[i][j];
    return r;
}
make_matrix4 :: (m: Matrix3) -> Matrix4 {
    r := Matrix4_Identity;
    for i: 0..2  for j: 0..2  r.coef[i][j] = m.coef[i][j];
    return r;
}

// A right-handed view matrix: the camera at `eye` looks at `target`.
make_look_at_matrix :: (eye: Vector3, target: Vector3, up: Vector3) -> Matrix4 {
    f := normalize(target - eye);
    s := normalize(cross(f, up));
    u := cross(s, f);
    m := Matrix4_Identity;
    m._11 = s.x;  m._12 = s.y;  m._13 = s.z;  m._14 = -dot(s, eye);
    m._21 = u.x;  m._22 = u.y;  m._23 = u.z;  m._24 = -dot(u, eye);
    m._31 = -f.x; m._32 = -f.y; m._33 = -f.z; m._34 = dot(f, eye);
    return m;
}

// A right-handed perspective projection to clip space with z in [-1, 1]
// (OpenGL's); `fov_vertical` in radians.
make_projection_matrix :: (fov_vertical: float32, aspect_ratio: float32, z_near: float32, z_far: float32) -> Matrix4 {
    f := 1 / tan(fov_vertical / 2);
    m: Matrix4;
    m._11 = f / aspect_ratio;
    m._22 = f;
    m._33 = (z_far + z_near) / (z_near - z_far);
    m._34 = 2 * z_far * z_near / (z_near - z_far);
    m._43 = -1;
    return m;
}

make_orthographic_projection_matrix :: (left: float32, right: float32, bottom: float32, top: float32,
                                        z_near: float32, z_far: float32) -> Matrix4 {
    m := Matrix4_Identity;
    m._11 = 2 / (right - left);
    m._22 = 2 / (top - bottom);
    m._33 = -2 / (z_far - z_near);
    m._14 = -(right + left) / (right - left);
    m._24 = -(top + bottom) / (top - bottom);
    m._34 = -(z_far + z_near) / (z_far - z_near);
    return m;
}

// ------------------------------------------------------------------ quaternions
//
// x, y, z: the vector part, w: the scalar part. A unit quaternion is a rotation.

Quaternion :: struct {
    union {
        struct { x, y, z, w: float32; }
        component: [4] float32;
    }
}

quaternion_identity :: () -> Quaternion { return make_quaternion(0, 0, 0, 1); }
Quaternion_Identity :: #run quaternion_identity();

make_quaternion :: (x: float32, y: float32, z: float32, w: float32) -> Quaternion {
    q: Quaternion = ---;
    q.x = x; q.y = y; q.z = z; q.w = w;
    return q;
}

// The rotation by `angle` radians around `axis` (normalized here).
set_from_axis_and_angle :: (q: *Quaternion, axis: Vector3, angle: float32) {
    a := normalize(axis);
    s := sin(angle / 2);
    q.x = a.x * s; q.y = a.y * s; q.z = a.z * s;
    q.w = cos(angle / 2);
}
make_quaternion_from_axis_and_angle :: (axis: Vector3, angle: float32) -> Quaternion {
    q: Quaternion = ---;
    set_from_axis_and_angle(*q, axis, angle);
    return q;
}

// The axis and angle of rotation q (a unit quaternion).
get_axis_and_angle :: (q: Quaternion) -> axis: Vector3, angle: float32 {
    s := sqrt(1 - q.w * q.w);
    angle := 2 * atan2(s, q.w);
    if s < 0.000001  return make_vector3(1, 0, 0), angle;
    return make_vector3(q.x / s, q.y / s, q.z / s), angle;
}

// The Hamilton product: rotating by `a * b` rotates by b, then by a.
operator * :: (a: Quaternion, b: Quaternion) -> Quaternion {
    return make_quaternion(a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                           a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                           a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                           a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}
operator + :: (a: Quaternion, b: Quaternion) -> Quaternion { return make_quaternion(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w); }
operator - :: (a: Quaternion, b: Quaternion) -> Quaternion { return make_quaternion(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w); }
operator * :: (q: Quaternion, k: float32) -> Quaternion #symmetric { return make_quaternion(q.x * k, q.y * k, q.z * k, q.w * k); }
operator - :: (q: Quaternion) -> Quaternion { return make_quaternion(-q.x, -q.y, -q.z, -q.w); }
operator == :: (a: Quaternion, b: Quaternion) -> bool { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; }

dot :: (a: Quaternion, b: Quaternion) -> float32 { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
length :: (q: Quaternion) -> float32 { return sqrt(dot(q, q)); }
length_squared :: (q: Quaternion) -> float32 { return dot(q, q); }
conjugate :: (q: Quaternion) -> Quaternion { return make_quaternion(-q.x, -q.y, -q.z, q.w); }
inverse :: (q: Quaternion) -> Quaternion {
    n := dot(q, q);
    if n == 0  return q;
    return conjugate(q) * (1 / n);
}
normalize :: (q: Quaternion, epsilon: float32 = 0.000001) -> Quaternion {
    n := length(q);
    if n <= epsilon  return q;
    return q * (1 / n);
}

// v rotated by unit quaternion q.
rotate :: (v: Vector3, q: Quaternion) -> Vector3 {
    u := make_vector3(q.x, q.y, q.z);
    t := cross(u, v) * 2;
    return v + t * q.w + cross(u, t);
}

// Interpolations between unit quaternions (taking the shorter way).
nlerp :: (a: Quaternion, b: Quaternion, t: float32) -> Quaternion {
    c := ifx dot(a, b) < 0 then -b else b;
    return normalize(a + (c - a) * t);
}
slerp :: (a: Quaternion, b: Quaternion, t: float32) -> Quaternion {
    c := b;
    d := dot(a, b);
    if d < 0 { c = -b; d = -d; }
    if d > 0.9995  return nlerp(a, c, t);
    theta := atan2(sqrt(1 - d * d), d);
    s := sin(theta);
    return a * (sin((1 - t) * theta) / s) + c * (sin(t * theta) / s);
}

// The rotation matrix of unit quaternion q.
get_rotation_matrix :: (q: Quaternion) -> Matrix3 {
    m: Matrix3 = ---;
    x2, y2, z2 := q.x * q.x, q.y * q.y, q.z * q.z;
    xy, xz, yz := q.x * q.y, q.x * q.z, q.y * q.z;
    wx, wy, wz := q.w * q.x, q.w * q.y, q.w * q.z;
    m._11 = 1 - 2 * (y2 + z2); m._12 = 2 * (xy - wz);     m._13 = 2 * (xz + wy);
    m._21 = 2 * (xy + wz);     m._22 = 1 - 2 * (x2 + z2); m._23 = 2 * (yz - wx);
    m._31 = 2 * (xz - wy);     m._32 = 2 * (yz + wx);     m._33 = 1 - 2 * (x2 + y2);
    return m;
}
rotation_matrix :: (q: Quaternion) -> Matrix4 { return make_matrix4(get_rotation_matrix(q)); }

#scope_file

// The cofactor of m's element (row, column): the signed determinant of the 3x3 matrix without them.
cofactor :: (m: Matrix4, row: int, column: int) -> float32 {
    s: Matrix3 = ---;
    si := 0;
    for i: 0..3 {
        if i == row  continue;
        sj := 0;
        for j: 0..3 {
            if j == column  continue;
            s.coef[si][sj] = m.coef[i][j];
            sj += 1;
        }
        si += 1;
    }
    d := determinant(s);
    return ifx (row + column) % 2 then -d else d;
}

// The cofactors of the first row.
cofactors :: (m: Matrix4) -> [4] float32 {
    c: [4] float32;
    for j: 0..3  c[j] = cofactor(m, 0, j);
    return c;
}
