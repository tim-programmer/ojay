// The part of Math written in Jai: clamp, lerp, PI64 / TAU64 and the vector
// types with their operators (also under #run).
// COMPILE_OUTPUT: #run: {4, 5, 6} length 13 clamp 10
#import "Basic";
#import "Math";

compile_time :: () -> float {
    a := make_vector3(3, 4, 12);
    b := a + make_vector3(1, 1, 1) - make_vector3(0, 0, 7);
    print("#run: % length % clamp %\n", b, length(a), clamp(25, -10, 10));
    return length(a);
}

COMPILE_TIME :: #run compile_time();

// A user type with the same operators: no clash with Math's.
My_Vec :: struct { a, b: int; }
operator + :: (x: My_Vec, y: My_Vec) -> My_Vec { return .{x.a + y.a, x.b + y.b}; }

main :: () {
    s: s32 = 7;
    print("clamp: % % % %\n", clamp(5, 0, 3), clamp(s, 0, 10), clamp(-1.5, 0.0, 1.0), clamp(0.25, 0.0, 1.0));
    print("lerp: % %\n", lerp(2.0, 4.0, 0.25), lerp(10, 20, 1.5));
    print("PI64 %, TAU64 / PI64 = %, type %\n", PI64 > 3.14159265358 && PI64 < 3.14159265359, TAU64 / PI64, type_of(PI64));

    p := make_vector2(3, 4);
    print("Vector2: % length % squared % normalized %\n", p, length(p), length_squared(p), normalize(p));
    print("  p.x % p.component[1] % size %\n", p.x, p.component[1], size_of(Vector2));

    a := make_vector3(1, 2, 3);
    b := make_vector3(4, 5, 6);
    print("Vector3: a + b = %, a - b = %, a * b = %, b / a = %\n", a + b, a - b, a * b, b / a);
    print("  a * 2 = %, 2 * a = %, b / 2 = %, -a = %\n", a * 2, 2 * a, b / 2, -a);
    print("  dot % cross % distance %\n", dot(a, b), cross(a, b), distance(make_vector3(1, 1, 1), make_vector3(4, 5, 1)));
    print("  a == a % a == b %\n", a == a, a == b);
    c := a;
    c += b;
    c *= 0.5;
    c.component[2] = 10;
    print("  c = % (c.z %)\n", c, c.z);
    print("  lerp % normalize(zero) %\n", lerp(a, b, 0.5), normalize(Vector3.{}));
    z := cross(make_vector3(1, 0, 0), make_vector3(0, 1, 0));
    print("  x cross y = % (unit: %)\n", z, length(normalize(b)) > 0.9999 && length(normalize(b)) < 1.0001);

    q := make_vector4(1, 2, 3, 4);
    print("Vector4: % dot % w % size %\n", q * 2 - q, dot(q, q), q.w, size_of(Vector4));

    m := My_Vec.{1, 2} + My_Vec.{3, 4};
    print("My_Vec: %\n", m);
    log("Basic's log and Math's log: % %", log(1.0), floor(log(100.0)));
    print("COMPILE_TIME: %\n", COMPILE_TIME);
}
