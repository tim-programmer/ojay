#import "Basic";

// An enum-driven state machine with a #complete switch.

Light :: enum u8 { RED; RED_AMBER; GREEN; AMBER; }

next :: (l: Light) -> Light {
    if #complete l == {
        case .RED;       return .RED_AMBER;
        case .RED_AMBER; return .GREEN;
        case .GREEN;     return .AMBER;
        case .AMBER;     return .RED;
    }
    return .RED;
}

duration :: (l: Light) -> int {
    if l == {
        case .RED; #through;
        case .GREEN;
            return 30;
        case;
            return 5;
    }
}

Event :: enum_flags { NONE :: 0; BUTTON; SENSOR; EMERGENCY; }

main :: () {
    light := Light.RED;
    t := 0;
    for cycle: 1..2 {
        for 1..4 {
            print("t=% % for %s\n", t, light, duration(light));
            t += duration(light);
            light = next(light);
        }
    }
    print("size_of(Light) = %, last value = %\n", size_of(Light), cast(u8) Light.AMBER);

    events := Event.BUTTON | .EMERGENCY;
    print("events = %\n", events);
    if events & .EMERGENCY then print("emergency!\n");
    if !(events & .SENSOR) then print("no sensor event\n");
    events &= ~Event.BUTTON;
    print("after clearing BUTTON: %\n", events);
    none: Event;
    print("none = %\n", none);

    // Search a grid with a labeled break.
    grid := int.[3, 8, 1, 9, 4, 7, 6, 2, 5];
    found_at := -1;
    for row: 0..2 {
        for col: 0..2 {
            if grid[row * 3 + col] == 7 {
                found_at = row * 3 + col;
                break row;
            }
        }
    }
    print("7 is at index %\n", found_at);
}
