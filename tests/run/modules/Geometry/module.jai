// A module in a directory. It imports what it needs itself.
#import "Math";
#load "shapes.jai";

GOLDEN :: 1.618;

area :: (c: Circle) -> float { return PI * c.radius * c.radius; }
area :: (r: Rect) -> float { return r.w * r.h; }

helper :: () -> string { return "Geometry.helper"; }   // no clash with Util.helper
