const std = @import("std");
const builtin = @import("builtin");

const time = std.time;
const posix = std.posix;

const state = @import("state.zig");

const RAND_MASK: u64 = 0x3FFF_FFFF_FFFF_FFFF;
const RESEED_MASK: u64 = (1 << 41) - 1;
const LOW30_MASK: u64 = (1 << 30) - 1;

pub fn nowMs() u64 {
    const now = time.Instant.now() catch unreachable;
    const timestamp = now.timestamp;

    if (builtin.os.tag == .windows) {
        return @as(u64, @intCast(timestamp / time.ns_per_ms));
    }

    return ms: {
        const sec = @as(u64, @intCast(timestamp.sec)) * std.time.ms_per_s;
        const nsec = @as(u64, @intCast(timestamp.nsec)) / std.time.ns_per_ms;
        break :ms sec + nsec;
    };
}

pub fn fillRandom(buf: []u8) !void {
    try posix.getrandom(buf);
}

fn unpackU64Be(bytes: *const [8]u8) u64 {
    return std.mem.readInt(u64, bytes, .big);
}

fn u64Secure() u64 {
    var bytes: [8]u8 = undefined;
    std.crypto.random.bytes(&bytes);
    return unpackU64Be(&bytes);
}

pub fn ensureSeeded() !void {
    if (state.runtime.prng_seeded) {
        return;
    }

    var seed: [24]u8 = undefined;
    try fillRandom(&seed);

    state.runtime.prng.seedWithBuf(seed);
    state.runtime.prng_seeded = true;
}

pub fn reseed() void {
    state.runtime.prng_seeded = false;
}

pub fn counter42() u64 {
    return state.runtime.prng.next() & RESEED_MASK;
}

pub fn counter42Secure(counter: *u64) !void {
    counter.* = u64Secure() & RESEED_MASK;
}

pub fn splitCounter42(counter: u64, low32: u32, rand_a: *u16, out_tail62: *u64) void {
    rand_a.* = @intCast(counter >> 30);
    out_tail62.* = ((counter & LOW30_MASK) << 32) | @as(u64, low32);
}

pub fn nextLow32AndIncrement(low32: *u32, increment: *u64) void {
    const random64 = state.runtime.prng.next();

    low32.* = @truncate(random64);
    increment.* = 1 + ((random64 >> 32) & 0x0F);
}

pub fn nextLow32AndIncrementSecure(low32: *u32, increment: *u64) !void {
    const random64 = u64Secure();

    low32.* = @truncate(random64);
    increment.* = 1 + ((random64 >> 32) & 0x0F);
}

pub fn payload(rand_a: *u16, out_tail62: *u64) void {
    splitCounter42(counter42(), @truncate(state.runtime.prng.next()), rand_a, out_tail62);
}

pub fn payloadSecure(rand_a: *u16, out_tail62: *u64) !void {
    var counter: u64 = 0;
    try counter42Secure(&counter);
    splitCounter42(counter, @truncate(u64Secure()), rand_a, out_tail62);
}

pub fn tail62() u64 {
    return state.runtime.prng.next() & RAND_MASK;
}

pub fn tail62Secure(out_tail62: *u64) !void {
    out_tail62.* = u64Secure() & RAND_MASK;
}
