// ERROR: duplicate case value
main :: () {
    s := "a";
    if s == { case "a"; case "b"; case "a"; }
}
