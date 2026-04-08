const std = @import("std");
const builtin = @import("builtin");
const state = @import("state.zig");

pub fn nowMs() u64 {
    if (builtin.os.tag == .linux) {
        return @as(u64, @intCast(@divFloor(std.time.nanoTimestamp(), std.time.ns_per_ms)));
    }
    const instant = std.time.Instant.now() catch unreachable;
    return @as(u64, @intCast(instant.timestamp / std.time.ns_per_ms));
}

pub fn fillRandom(buf: []u8) state.Status {
    std.posix.getrandom(buf) catch return .random_failure;
    return .ok;
}
