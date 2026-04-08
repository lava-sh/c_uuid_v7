const std = @import("std");
const builtin = @import("builtin");
const state = @import("state.zig");

const posix = std.posix;

pub fn nowMs() u64 {
    const instant = std.time.Instant.now() catch unreachable;
    return @as(u64, @intCast(instant.timestamp / std.time.ns_per_ms));
}

pub fn fillRandom(buf: []u8) state.Status {
    if (builtin.os.tag == .windows) {
        std.crypto.random.bytes(buf);
        return .ok;
    }

    posix.getrandom(buf) catch return .random_failure;
    return .ok;
}
