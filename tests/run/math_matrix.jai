#import "Basic";
#import "Math";

close :: (a: float32, b: float32) -> bool { return abs(a - b) < 0.0001; }
close :: (a: Vector3, b: Vector3) -> bool { return close(a.x, b.x) && close(a.y, b.y) && close(a.z, b.z); }
close :: (a: Matrix4, b: Matrix4) -> bool { for 0..15 if !close(a.floats[it], b.floats[it]) return false; return true; }

main :: () {
    t := make_translation_matrix4(make_vector3(1, 2, 3));
    s := make_scale_matrix4(make_vector3(2, 2, 2));
    m := t * s;
    p := transform_point(m, make_vector3(1, 1, 1));
    print("% %\n", p, transform_vector(m, make_vector3(1, 1, 1)));

    inv, ok := inverse(m);
    print("% %\n", ok, close(inv * m, Matrix4_Identity));
    print("% %\n", determinant(m), determinant(make_matrix3(m)));
    print("%\n", transpose(t)._41);
    _, singular_ok := inverse(Matrix4.{});
    print("%\n", singular_ok);

    m3 := Matrix3.{};
    m3._11 = 2; m3._12 = 1; m3._21 = 1; m3._22 = 3; m3._33 = 4;
    i3, ok3 := inverse(m3);
    print("% %\n", ok3, i3 * m3 == Matrix3_Identity || close(make_matrix4(i3 * m3), Matrix4_Identity));

    q := make_quaternion_from_axis_and_angle(make_vector3(0, 0, 1), PI / 2);
    r := rotate(make_vector3(1, 0, 0), q);
    print("%\n", close(r, make_vector3(0, 1, 0)));
    print("%\n", close(get_rotation_matrix(q) * make_vector3(1, 0, 0), r));
    q2 := q * q;  // 180 degrees
    print("%\n", close(rotate(make_vector3(1, 0, 0), q2), make_vector3(-1, 0, 0)));
    axis, angle := get_axis_and_angle(q);
    print("% %\n", close(axis, make_vector3(0, 0, 1)), close(angle, PI / 2));
    half := slerp(Quaternion_Identity, q, 0.5);
    print("%\n", close(rotate(make_vector3(1, 0, 0), half), make_vector3(0.70710678, 0.70710678, 0)));
    print("%\n", close(rotate(rotate(make_vector3(3, 4, 5), q), inverse(q)), make_vector3(3, 4, 5)));

    look := make_look_at_matrix(make_vector3(0, 0, 5), make_vector3(0, 0, 0), make_vector3(0, 1, 0));
    print("%\n", close(transform_point(look, make_vector3(0, 0, 0)), make_vector3(0, 0, -5)));
    proj := make_projection_matrix(PI / 2, 1, 1, 100);
    near := transform_point(proj, make_vector3(0, 0, -1));
    far := transform_point(proj, make_vector3(0, 0, -100));
    print("% %\n", close(near.z, -1), close(far.z, 1));
    ortho := make_orthographic_projection_matrix(-2, 2, -1, 1, 0, 10);
    print("%\n", close(transform_point(ortho, make_vector3(2, 1, -10)), make_vector3(1, 1, 1)));
}
