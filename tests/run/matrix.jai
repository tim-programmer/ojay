#import "Basic";
#import "Math";

// Fixed-size 2D arrays inside structs, passed by value and by pointer.

Mat3 :: struct {
    m: [3][3]float64;
}

identity :: () -> Mat3 {
    r: Mat3;
    for i: 0..2 r.m[i][i] = 1;
    return r;
}

mul :: (a: Mat3, b: Mat3) -> Mat3 {
    r: Mat3;
    for i: 0..2 for j: 0..2 {
        s: float64 = 0;
        for k: 0..2 s += a.m[i][k] * b.m[k][j];
        r.m[i][j] = s;
    }
    return r;
}

det :: (a: *Mat3) -> float64 {
    m := *a.m;
    return m.*[0][0] * (m.*[1][1] * m.*[2][2] - m.*[1][2] * m.*[2][1])
         - m.*[0][1] * (m.*[1][0] * m.*[2][2] - m.*[1][2] * m.*[2][0])
         + m.*[0][2] * (m.*[1][0] * m.*[2][1] - m.*[1][1] * m.*[2][0]);
}

transpose :: (a: *Mat3) {
    for i: 0..2 for j: i+1..2 {
        t := a.m[i][j];
        a.m[i][j] = a.m[j][i];
        a.m[j][i] = t;
    }
}

rotation_z :: (degrees: float64) -> Mat3 {
    r := identity();
    rad := degrees * PI / 180;
    c := cos(rad);
    s := sin(rad);
    r.m[0][0] = c;  r.m[0][1] = -s;
    r.m[1][0] = s;  r.m[1][1] = c;
    return r;
}

main :: () {
    a: Mat3;
    a.m = .[float64.[2, -3, 1], float64.[2, 0, -1], float64.[1, 4, 5]];
    print("a = %\n", a.m);
    print("det(a) = %\n", det(*a));
    print("a * I == a: %\n", mul(a, identity()).m[2][1] == a.m[2][1]);

    b := a;
    transpose(*b);
    print("transpose = %\n", b.m);
    print("det(transpose) = %\n", det(*b));

    p := mul(a, b);
    print("a * aT = %\n", p.m);

    r := mul(rotation_z(30), rotation_z(60));
    print("rot(30)*rot(60) ~ rot(90): % %\n", round(r.m[0][1]), round(r.m[1][0]));
    print("hypot: %\n", sqrt(3.0 * 3.0 + 4.0 * 4.0));
}
