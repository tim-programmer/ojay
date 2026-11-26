#import "Basic";

Color :: enum { RED; GREEN; BLUE; }
using Color;

Settings :: struct { volume := 7; fullscreen := true; }
settings: Settings;
using settings;

favorite :: GREEN;

describe :: (c: Color) -> string {
    if c == {
        case RED;   return "red";
        case GREEN; return "green";
        case;       return "other";
    }
}

main :: () {
    print("% % %\n", describe(RED), describe(favorite), describe(BLUE));
    volume += 1;
    print("% % %\n", volume, fullscreen, settings.volume);
}
