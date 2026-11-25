#import "Basic";

Direction :: enum { NORTH; EAST; SOUTH; WEST; }
Flags :: enum_flags u8 { READ; WRITE; EXEC; }
Level :: enum s16 { LOW :: -1; MID :: 5; HIGH; }

SIZE :: 4;
HALF :: SIZE / 2;
NAME :: "constant";
RATIO :: 1.5;
Index :: s32;

turn_right :: (d: Direction) -> Direction {
    if d == {
        case .NORTH; return .EAST;
        case .EAST;  return .SOUTH;
        case .SOUTH; return .WEST;
        case;        return .NORTH;
    }
}

main :: () {
    d := Direction.NORTH;
    for 1..4 {
        print("% ", d);
        d = turn_right(d);
    }
    print("\n");
    f := Flags.READ | .EXEC;
    print("flags = %, raw = %\n", f, cast(u8) f);
    print("has write: %\n", (f & .WRITE) != 0);
    print("levels: % % %\n", Level.LOW, Level.MID, cast(int) Level.HIGH);
    arr: [SIZE]int;
    print("SIZE=% HALF=% count=% NAME=% RATIO=%\n", SIZE, HALF, arr.count, NAME, RATIO);
    i: Index = 7;
    print("Index: % %\n", i, type_of(i));
    x: u8 = SIZE * 60;
    print("x = %\n", x);
    print("% %\n", Direction.WEST, cast(Direction) 1);
}
