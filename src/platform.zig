const builtin = @import("builtin");
const posix = @import("posix.zig");
const windows = @import("windows.zig");

pub fn nowMs() u64 {
    if (builtin.os.tag == .windows) {
        return windows.nowMs();
    }

    return posix.nowMs();
}

pub fn systemMs() u64 {
    if (builtin.os.tag == .windows) {
        return windows.systemMs();
    }

    return posix.systemMs();
}

pub fn fillRandom(buf: [*]u8, len: isize) c_int {
    if (builtin.os.tag == .windows) {
        return windows.fillRandom(buf, len);
    }

    return posix.fillRandom(buf, len);
}

pub fn platformSeeded() void {
    if (builtin.os.tag != .windows) {
        return posix.platformSeeded();
    }

    windows.platformSeeded();
}
