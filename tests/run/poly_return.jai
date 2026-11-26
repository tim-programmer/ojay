#import "Basic";

convert :: (x: $T) -> $R { return cast(R) x; }

zero :: () -> $R {
    r: R;
    return r;
}

make_array :: (n: int) -> [..] $E {
    result: [..] E;
    for 1..n  array_add(*result, cast(E) it);
    return result;
}

half :: (x: int) -> float64 { return convert(x) / 2; }

takes_float :: (f: float) { print("takes_float %\n", f); }

main :: () {
    a: float = convert(7);
    b: u8 = convert(200.0);
    print("% % % %\n", a, type_of(a), b, type_of(b));
    print("%\n", half(5));
    takes_float(convert(3));

    z: Vector3 = zero();
    n: int = zero();
    print("% %\n", z, n);

    arr: [..] float64 = make_array(3);
    print("% %\n", arr, type_of(arr));

    s: s16;
    s = convert(-2.75);
    print("%\n", s);
}

Vector3 :: struct { x, y, z: float; }
