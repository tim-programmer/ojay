// Module File: files, after Jai's module of the same name. On Linux it calls
// the C library. On bare metal (OS == .NONE) the files are the debugger's:
// semihosting file calls, relative to the directory the debugger runs in. Each
// call stops the core while the debugger does it, and without a debugger that
// handles semihosting every call fails (file_open returns false).

#import "Basic";

#assert OS == .LINUX || OS == .NONE, "the File module needs Linux (the C library) or a debugger's semihosting (bare metal)";

File :: struct {
    handle: s32 = -1;  // the file descriptor
}

File_Seek_Type :: enum { START; CURRENT; END; }

#if OS == .LINUX {
// Opens `name` for reading, or for writing (created if needed; emptied
// unless keep_existing_content). Returns the file and whether it worked.
file_open :: (name: string, for_writing := false, keep_existing_content := false, log_errors := true) -> File, bool {
    flags: s32 = O_RDONLY;
    if for_writing {
        flags = O_RDWR | O_CREAT;
        if !keep_existing_content  flags |= O_TRUNC;
    }
    c_name := to_c_string(name);
    fd := libc_open(c_name, flags, 0x1A4);
    free(c_name);
    f: File;
    if fd < 0 {
        if log_errors  log_error("Could not open file \"%\".", name);
        return f, false;
    }
    f.handle = fd;
    return f, true;
}

file_close :: (f: *File) {
    if f.handle >= 0  libc_close(f.handle);
    f.handle = -1;
}

// Writes all of `data`. Returns whether it worked.
file_write :: (f: *File, data: string) -> bool { return file_write(f, data.data, data.count); }
file_write :: (f: *File, data: *void, count: s64) -> bool {
    written: s64 = 0;
    while written < count {
        n := libc_write(f.handle, cast(*u8) data + written, cast(u64) (count - written));
        if n <= 0  return false;
        written += n;
    }
    return true;
}

// Reads up to `bytes_to_read` bytes into `buffer`. Returns whether it worked, and how many it read.
file_read :: (f: File, buffer: *void, bytes_to_read: s64) -> success: bool, total_read: s64 {
    total: s64 = 0;
    while total < bytes_to_read {
        n := libc_read(f.handle, cast(*u8) buffer + total, cast(u64) (bytes_to_read - total));
        if n < 0  return false, total;
        if n == 0  break;
        total += n;
    }
    return true, total;
}

// The file's length in bytes (the position is kept).
file_length :: (f: File) -> s64, bool {
    here := libc_lseek(f.handle, 0, SEEK_CUR);
    end := libc_lseek(f.handle, 0, SEEK_END);
    if here < 0 || end < 0  return 0, false;
    libc_lseek(f.handle, here, SEEK_SET);
    return end, true;
}

file_seek :: (f: File, position: s64, type := File_Seek_Type.START) -> bool {
    whence: s32 = ifx type == .START then SEEK_SET else ifx type == .CURRENT then SEEK_CUR else SEEK_END;
    return libc_lseek(f.handle, position, whence) >= 0;
}

file_tell :: (f: File) -> s64, bool {
    p := libc_lseek(f.handle, 0, SEEK_CUR);
    return p, p >= 0;
}

file_exists :: (name: string) -> bool {
    c_name := to_c_string(name);
    defer free(c_name);
    return libc_access(c_name, F_OK) == 0;
}

file_delete :: (name: string) -> bool {
    c_name := to_c_string(name);
    defer free(c_name);
    return libc_unlink(c_name) == 0;
}

// Creates directory `name` unless it exists. Returns whether it exists now.
make_directory_if_it_does_not_exist :: (name: string) -> bool {
    c_name := to_c_string(name);
    defer free(c_name);
    if libc_mkdir(c_name, 0x1ED) == 0  return true;
    return libc_access(c_name, F_OK) == 0;
}

// Renames (moves) file `old_name` to `new_name`. Returns whether it worked.
file_move :: (old_name: string, new_name: string) -> bool {
    c_old := to_c_string(old_name);
    defer free(c_old);
    c_new := to_c_string(new_name);
    defer free(c_new);
    return libc_rename(c_old, c_new) == 0;
}

} else {

// Opens `name` for reading, or for writing (created if needed; emptied
// unless keep_existing_content). Returns the file and whether it worked.
file_open :: (name: string, for_writing := false, keep_existing_content := false, log_errors := true) -> File, bool {
    f: File;
    handle: s32 = -1;
    if !for_writing {
        handle = sh_open(name, MODE_RB);
    } else {
        if keep_existing_content  handle = sh_open(name, MODE_RPLUSB);
        if handle < 0  handle = sh_open(name, MODE_WPLUSB);
    }
    if handle < 0 || !track(handle) {
        if handle >= 0  sh_close(handle);
        if log_errors  log_error("Could not open file \"%\".", name);
        return f, false;
    }
    f.handle = handle;
    return f, true;
}

file_close :: (f: *File) {
    if f.handle >= 0 {
        sh_close(f.handle);
        untrack(f.handle);
    }
    f.handle = -1;
}

// Writes all of `data`. Returns whether it worked.
file_write :: (f: *File, data: string) -> bool { return file_write(f, data.data, data.count); }
file_write :: (f: *File, data: *void, count: s64) -> bool {
    if count == 0  return true;
    args: [3] u32 = .[cast(u32) f.handle, word(data), cast(u32) count];
    not_written := __ojay_semihost_call(SYS_WRITE, *args);
    if not_written < 0 || not_written > count  return false;
    position := tracked(f.handle);
    if position  position.* += count - not_written;
    return not_written == 0;
}

// Reads up to `bytes_to_read` bytes into `buffer`. Returns whether it worked, and how many it read.
file_read :: (f: File, buffer: *void, bytes_to_read: s64) -> success: bool, total_read: s64 {
    if bytes_to_read == 0  return true, 0;
    args: [3] u32 = .[cast(u32) f.handle, word(buffer), cast(u32) bytes_to_read];
    not_read := __ojay_semihost_call(SYS_READ, *args);
    if not_read < 0 || not_read > bytes_to_read  return false, 0;
    n := bytes_to_read - not_read;
    position := tracked(f.handle);
    if position  position.* += n;
    return true, n;
}

// The file's length in bytes (the position is kept).
file_length :: (f: File) -> s64, bool {
    args: [1] u32 = .[cast(u32) f.handle];
    n := __ojay_semihost_call(SYS_FLEN, *args);
    return ifx n < 0 then 0 else cast(s64) n, n >= 0;
}

file_seek :: (f: File, position: s64, type := File_Seek_Type.START) -> bool {
    current := tracked(f.handle);
    if !current  return false;
    target := position;
    if type == .CURRENT  target += current.*;
    if type == .END {
        length, ok := file_length(f);
        if !ok  return false;
        target += length;
    }
    if target < 0  return false;
    args: [2] u32 = .[cast(u32) f.handle, cast(u32) target];
    if __ojay_semihost_call(SYS_SEEK, *args) != 0  return false;
    current.* = target;
    return true;
}

file_tell :: (f: File) -> s64, bool {
    p := tracked(f.handle);
    if !p  return 0, false;
    return p.*, true;
}

file_exists :: (name: string) -> bool {
    handle := sh_open(name, MODE_RB);
    if handle < 0  return false;
    sh_close(handle);
    return true;
}

file_delete :: (name: string) -> bool {
    args: [2] u32 = .[word(name.data), cast(u32) name.count];
    return __ojay_semihost_call(SYS_REMOVE, *args) == 0;
}

// Renames (moves) file `old_name` to `new_name`. Returns whether it worked.
file_move :: (old_name: string, new_name: string) -> bool {
    args: [4] u32 = .[word(old_name.data), cast(u32) old_name.count, word(new_name.data), cast(u32) new_name.count];
    return __ojay_semihost_call(SYS_RENAME, *args) == 0;
}

// Semihosting cannot make directories: false.
make_directory_if_it_does_not_exist :: (name: string) -> bool { return false; }

}

// The whole file as a string (allocated with context.allocator), and whether it worked.
read_entire_file :: (name: string, zero_terminated := false, log_errors := true) -> string, bool {
    f, ok := file_open(name, log_errors = log_errors);
    if !ok  return "", false;
    defer file_close(*f);
    length, ok_length := file_length(f);
    if !ok_length {
        if log_errors  log_error("Could not get the length of \"%\".", name);
        return "", false;
    }
    result: string;
    result.data = alloc(length + (ifx zero_terminated then 1 else 0));
    ok_read, n := file_read(f, result.data, length);
    if !ok_read {
        if log_errors  log_error("Could not read \"%\".", name);
        free(result.data);
        return "", false;
    }
    result.count = n;
    if zero_terminated  result.data[n] = 0;
    return result, true;
}

// Writes `data` as the whole content of `name` (created or replaced). Returns whether it worked.
write_entire_file :: (name: string, data: string) -> bool { return write_entire_file(name, data.data, data.count); }
write_entire_file :: (name: string, data: *void, count: s64) -> bool {
    f, ok := file_open(name, for_writing = true);
    if !ok  return false;
    defer file_close(*f);
    return file_write(*f, data, count);
}

#scope_file

#if OS == .LINUX {


libc :: #system_library "libc";

libc_open   :: (path: *u8, flags: s32, mode: u32) -> s32 #foreign libc "open";
libc_close  :: (fd: s32) -> s32 #foreign libc "close";
libc_read   :: (fd: s32, buffer: *void, count: u64) -> s64 #foreign libc "read";
libc_write  :: (fd: s32, buffer: *void, count: u64) -> s64 #foreign libc "write";
libc_lseek  :: (fd: s32, offset: s64, whence: s32) -> s64 #foreign libc "lseek";
libc_access :: (path: *u8, mode: s32) -> s32 #foreign libc "access";
libc_unlink :: (path: *u8) -> s32 #foreign libc "unlink";
libc_mkdir  :: (path: *u8, mode: u32) -> s32 #foreign libc "mkdir";
libc_rename :: (old: *u8, new: *u8) -> s32 #foreign libc "rename";

O_RDONLY :: 0;
O_RDWR   :: 2;
O_CREAT  :: 0x40;
O_TRUNC  :: 0x200;
SEEK_SET :: 0;
SEEK_CUR :: 1;
SEEK_END :: 2;
F_OK     :: 0;

} else {

// Semihosting (runtime/ojay_runtime.c: -1 when no debugger handles it).
runtime :: #system_library "libc";  // (resolved against the runtime library at link time)
__ojay_semihost_call :: (op: s32, args: *void) -> s32 #foreign runtime;

SYS_OPEN   :: 0x01;
SYS_CLOSE  :: 0x02;
SYS_WRITE  :: 0x05;
SYS_READ   :: 0x06;
SYS_SEEK   :: 0x0A;
SYS_FLEN   :: 0x0C;
SYS_REMOVE :: 0x0E;
SYS_RENAME :: 0x0F;

MODE_RB     :: 1;  // "rb"
MODE_RPLUSB :: 3;  // "r+b"
MODE_WPLUSB :: 7;  // "w+b"

word :: (p: *void) -> u32 { return cast(u32) cast(u64) p; }

sh_open :: (name: string, mode: u32) -> s32 {
    c_name := to_c_string(name);  // (semihosting wants it terminated too)
    defer free(c_name);
    args: [3] u32 = .[word(c_name), mode, cast(u32) name.count];
    return __ojay_semihost_call(SYS_OPEN, *args);
}

sh_close :: (handle: s32) {
    args: [1] u32 = .[cast(u32) handle];
    __ojay_semihost_call(SYS_CLOSE, *args);
}

// Semihosting has no "tell": the module keeps each open file's position.
Open_File :: struct {
    handle: s32 = -1;
    position: s64;
}
open_files: [16] Open_File;

track :: (handle: s32) -> bool {
    for * open_files  if it.handle < 0 {
        it.handle = handle;
        it.position = 0;
        return true;
    }
    return false;  // too many open files
}
untrack :: (handle: s32) {
    for * open_files  if it.handle == handle  it.handle = -1;
}
tracked :: (handle: s32) -> *s64 {
    for * open_files  if it.handle == handle  return *it.position;
    return null;
}

}
