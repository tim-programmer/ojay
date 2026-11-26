#import "Basic";

Color :: struct { r, g, b: u8; a: u8 = 255; }
Style :: struct { name: string; fg: Color; bg := Color.{0, 0, 0, 0}; sizes: [2] int; }

RED :: Color.{255, 0, 0};
TITLE :: Style.{name = "title", fg = RED};
#assert RED.a == 255;
#assert TITLE.fg.r == 255;

TABLE :: Color.[RED, .{0, 255, 0}];

main :: () {
    print("% %\n", RED, TITLE);
    print("%\n", TABLE);
    i := 1;
    print("%\n", TABLE[i]);
    s := TITLE;
    s.sizes[1] = 7;
    print("% %\n", s.sizes, TITLE.sizes);
}
