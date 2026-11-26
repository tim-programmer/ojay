// Module Console (ojay's own; Jai has no module for this): reading the
// program's console input. On Linux that is standard input. On the boards it
// is the debugger's console through semihosting when a debugger handles it
// (OpenOCD's standard input), otherwise the board's console: the MCX N947's
// UART (the MCU-Link's virtual COM port), the ESP32-C6's USB Serial/JTAG
// port, or the program's own `__ojay_console_read`. A terminal's Enter (CR or
// CR LF) arrives as "\n" there. Reading waits (on bare metal the whole
// program waits: threads do not run meanwhile).

#import "Basic";

// Reads the input there is, up to `count` bytes, waiting until there is some.
// Returns how many it read: 0 at the end of the input, -1 when the program
// has no console input.
console_read :: (buffer: *u8, count: s64) -> s64 {
    if count <= 0  return 0;
    if pending_count > 0 {
        n := ifx count < pending_count then count else pending_count;
        memcpy(buffer, pending.data + pending_start, n);
        pending_start += n;
        pending_count -= n;
        return n;
    }
    return __ojay_read_input(buffer, count);
}

// Reads one line, without its "\n" (or "\r\n"), allocated with
// context.allocator. Returns false at the end of the input (or without
// console input) when there was nothing left; a last line without "\n" is
// returned as a line.
console_read_line :: () -> string, bool {
    line: [..] u8;
    while true {
        if pending_count == 0 {
            n := __ojay_read_input(pending.data, pending.count);
            if n <= 0 {
                if line.count == 0  return "", false;
                break;
            }
            pending_start = 0;
            pending_count = n;
        }
        newline := -1;
        for i: 0..pending_count - 1  if pending[pending_start + i] == #char "\n" {
            newline = i;
            break;
        }
        take := ifx newline < 0 then pending_count else newline;
        for i: 0..take - 1  array_add(*line, pending[pending_start + i]);
        if newline < 0 {
            pending_count = 0;
            continue;
        }
        pending_start += newline + 1;
        pending_count -= newline + 1;
        break;
    }
    if line.count > 0 && line[line.count - 1] == #char "\r"  line.count -= 1;
    result: string;
    result.data = line.data;
    result.count = line.count;
    return result, true;
}

#scope_file

runtime :: #system_library "libc";  // (the runtime library, at link time)
__ojay_read_input :: (data: *u8, count: s64) -> s64 #foreign runtime;

// Input read beyond the last line console_read_line returned.
pending: [256] u8;
pending_start: s64;
pending_count: s64;
