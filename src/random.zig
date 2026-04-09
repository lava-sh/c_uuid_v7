const std = @import("std");

const time = std.time;
const posix = std.posix;

const state = @import("state.zig");

pub fn nowMs() u64 {
    const now = time.Instant.now() catch unreachable;
    return @as(u64, @intCast(now.timestamp / time.ns_per_ms));
}

pub fn fillRandom(buf: []u8) state.Status {
    posix.getrandom(buf) catch return .random_failure;
    return .ok;
}

fn unpackU64Be(bytes: *const [8]u8) u64 {
    return std.mem.readInt(u64, bytes, .big);
}

fn u64Secure() u64 {
    var bytes: [8]u8 = undefined;
    std.crypto.random.bytes(&bytes);
    return unpackU64Be(&bytes);
}

pub fn ensureSeeded() state.Status {
    var seed: [24]u8 = undefined;

    if (state.runtime.prng_seeded) {
        return .ok;
    }

    if (fillRandom(&seed) != .ok) {
        return .random_failure;
    }

    state.runtime.prng.seedWithBuf(seed);
    state.runtime.prng_seeded = true;
    return .ok;
}

pub fn reseed() void {
    state.runtime.prng_seeded = false;
}

pub fn counter42() u64 {
    return state.runtime.prng.next() & state.RESEED_MASK;
}

pub fn counter42Secure(counter: *u64) state.Status {
    counter.* = u64Secure() & state.RESEED_MASK;
    return .ok;
}

pub fn splitCounter42(counter: u64, low32: u32, rand_a: *u16, out_tail62: *u64) void {
    rand_a.* = @intCast(counter >> 30);
    out_tail62.* = ((counter & state.LOW30_MASK) << 32) | @as(u64, low32);
}

pub fn nextLow32AndIncrement(low32: *u32, increment: *u64) void {
    const random64 = state.runtime.prng.next();

    low32.* = @truncate(random64);
    increment.* = 1 + ((random64 >> 32) & 0x0F);
}

pub fn nextLow32AndIncrementSecure(low32: *u32, increment: *u64) state.Status {
    const random64 = u64Secure();

    low32.* = @truncate(random64);
    increment.* = 1 + ((random64 >> 32) & 0x0F);
    return .ok;
}

pub fn payload(rand_a: *u16, out_tail62: *u64) void {
    splitCounter42(counter42(), @truncate(state.runtime.prng.next()), rand_a, out_tail62);
}

pub fn payloadSecure(rand_a: *u16, out_tail62: *u64) state.Status {
    var counter: u64 = 0;

    if (counter42Secure(&counter) != .ok) {
        return .random_failure;
    }

    splitCounter42(counter, @truncate(u64Secure()), rand_a, out_tail62);
    return .ok;
}

pub fn tail62() u64 {
    return state.runtime.prng.next() & state.RAND_MASK;
}

pub fn tail62Secure(out_tail62: *u64) state.Status {
    out_tail62.* = u64Secure() & state.RAND_MASK;
    return .ok;
}
