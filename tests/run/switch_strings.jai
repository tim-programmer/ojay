#import "Basic";

describe :: (command: string) -> string {
    if command == {
      case "go"; #through;
      case "run";
        return "move";
      case "stop", "halt";
        return "stop";
      case "";
        return "empty";
      case;
        return "unknown";
    }
}

main :: () {
    words := string.["go", "run", "stop", "halt", "", "jump", "stopped"];
    for words print("% -> %\n", it, describe(it));
    built := sprint("ha%", "lt");
    if built == {
      case "halt"; print("built string matched\n");
      case; print("no match\n");
    }
}
