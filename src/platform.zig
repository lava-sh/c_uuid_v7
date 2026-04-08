const std = @import("std");
const builtin = @import("builtin");
const state = @import("state.zig");

pub fn nowMs() u64 {
    const instant = std.time.Instant.now() catch unreachable;
    if (builtin.os.tag == .linux) {
        const ts = instant.timestamp;
        const ns = @as(i128, @intCast(ts.tv_sec)) * std.time.ns_per_s + @as(i128, @intCast(ts.tv_nsec));
        return @as(u64, @intCast(ns / std.time.ns_per_ms));
    }
    return @as(u64, @intCast(instant.timestamp / std.time.ns_per_ms));
}

pub fn fillRandom(buf: []u8) state.Status {
    if (builtin.os.tag == .windows) {
        std.crypto.random.bytes(buf);
        return .ok;
    }

    std.posix.getrandom(buf) catch return .random_failure;
    return .ok;
}
