#import "Basic";
#import "Util";                 // Util's declarations are visible directly
G :: #import "Geometry";        // Geometry's only as G.name
M :: #import "Math";

helper :: () -> string { return "main.helper"; }    // the program's own declaration wins
describe :: (s: string) -> string { return sprint("string %", s); }   // overloads Util.describe

main :: () {
    print("%\n", helper());
    print("%\n", G.helper());
    print("% / %\n", describe(3), describe("x"));   // overloads from two modules
    shout("hello");

    c := G.Circle.{2};
    r: G.Rect = .{3, 4};
    print("% %\n", cast(int) G.area(c), G.area(r));
    print("% %\n", G.GOLDEN, M.floor(2.5));
    f: (G.Rect) -> float = G.area;
    print("%\n", f(.{1, 2}));
}
