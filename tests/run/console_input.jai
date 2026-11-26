// Console input (ojay's Console module): standard input on Linux; on the
// boards the debugger's console through semihosting, or the UART.
// STDIN: first line
// STDIN: second: words, punctuation
// STDIN:
// STDIN: after an empty line
#import "Basic";
#import "Console";

main :: () {
    print("ready\n");  // (input for a UART run is sent once the program printed something)
    count := 0;
    while true {
        line, ok := console_read_line();
        if !ok  break;
        count += 1;
        print("% [%] %\n", count, line, line.count);
    }
    buffer: [4] u8;
    print("end % %\n", count, console_read(buffer.data, 4));
}
