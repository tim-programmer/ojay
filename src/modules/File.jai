// Module File: files, after Jai's module of the same name. Linux only (it
// calls the C library); bare-metal programs cannot import it.

#import "Basic";

#assert OS == .LINUX, "the File module needs an operating system (Linux): it calls the C library";

File :: struct {
    handle: s32 = -1;  // the file descriptor
}

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

File_Seek_Type :: enum { START; CURRENT; END; }

file_seek :: (f: File, position: s64, type := File_Seek_Type.START) -> bool {
    whence: s32 = ifx type == .START then SEEK_SET else ifx type == .CURRENT then SEEK_CUR else SEEK_END;
    return libc_lseek(f.handle, position, whence) >= 0;
}

file_tell :: (f: File) -> s64, bool {
    p := libc_lseek(f.handle, 0, SEEK_CUR);
    return p, p >= 0;
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

#scope_file

libc :: #system_library "libc";

libc_open   :: (path: *u8, flags: s32, mode: u32) -> s32 #foreign libc "open";
libc_close  :: (fd: s32) -> s32 #foreign libc "close";
libc_read   :: (fd: s32, buffer: *void, count: u64) -> s64 #foreign libc "read";
libc_write  :: (fd: s32, buffer: *void, count: u64) -> s64 #foreign libc "write";
libc_lseek  :: (fd: s32, offset: s64, whence: s32) -> s64 #foreign libc "lseek";
libc_access :: (path: *u8, mode: s32) -> s32 #foreign libc "access";
libc_unlink :: (path: *u8) -> s32 #foreign libc "unlink";
libc_mkdir  :: (path: *u8, mode: u32) -> s32 #foreign libc "mkdir";

O_RDONLY :: 0;
O_RDWR   :: 2;
O_CREAT  :: 0x40;
O_TRUNC  :: 0x200;
SEEK_SET :: 0;
SEEK_CUR :: 1;
SEEK_END :: 2;
F_OK     :: 0;
