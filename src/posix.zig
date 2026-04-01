const std = @import("std");

pub fn nowMs() u64 {
    return systemMs();
}

pub fn systemMs() u64 {
    return @intCast(std.time.milliTimestamp());
}

pub fn fillRandom(buf: [*]u8, len: isize) c_int {
    const bytes = buf[0..@intCast(len)];

    std.posix.getrandom(bytes) catch {
        const file = std.fs.openFileAbsolute("/dev/urandom", .{}) catch return -1;
        defer file.close();

        const size = file.readAll(bytes) catch return -1;
        return if (size == bytes.len) 0 else -1;
    };

    return 0;
}

pub fn platformSeeded() void {}
