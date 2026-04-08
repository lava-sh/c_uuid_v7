const std = @import("std");
const builtin = @import("builtin");

const platform = @import("platform.zig");
const state = @import("state.zig");

fn unpackU64Be(bytes: *const [8]u8) u64 {
    return std.mem.readInt(u64, bytes, .big);
}

fn u64Secure() u64 {
    var bytes: [8]u8 = undefined;
    std.crypto.random.bytes(&bytes);
    return unpackU64Be(&bytes);
}

pub fn ensureSeeded() state.Int {
    var seed: [24]u8 = undefined;

    if (state.runtime.prng_seeded) {
        return state.STATUS_OK;
    }

    if (platform.fillRandom(&seed, seed.len) != state.STATUS_OK) {
        return state.STATUS_RANDOM_FAILURE;
    }

    state.runtime.prng.seedWithBuf(seed);

    if (builtin.os.tag == .windows) {
        platform.platformSeeded();
    }

    state.runtime.prng_seeded = true;
    return state.STATUS_OK;
}

pub fn reseed() void {
    state.runtime.prng_seeded = false;
}

pub fn counter42() u64 {
    return state.runtime.prng.next() & state.RESEED_MASK;
}

pub fn counter42Secure(counter: *u64) state.Int {
    counter.* = u64Secure() & state.RESEED_MASK;
    return state.STATUS_OK;
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

pub fn nextLow32AndIncrementSecure(low32: *u32, increment: *u64) state.Int {
    const random64 = u64Secure();

    low32.* = @truncate(random64);
    increment.* = 1 + ((random64 >> 32) & 0x0F);
    return state.STATUS_OK;
}

pub fn payload(rand_a: *u16, out_tail62: *u64) void {
    splitCounter42(counter42(), @truncate(state.runtime.prng.next()), rand_a, out_tail62);
}

pub fn payloadSecure(rand_a: *u16, out_tail62: *u64) state.Int {
    var counter: u64 = 0;

    if (counter42Secure(&counter) != state.STATUS_OK) {
        return state.STATUS_RANDOM_FAILURE;
    }

    splitCounter42(counter, @truncate(u64Secure()), rand_a, out_tail62);
    return state.STATUS_OK;
}

pub fn tail62() u64 {
    return state.runtime.prng.next() & state.RAND_MASK;
}

pub fn tail62Secure(out_tail62: *u64) state.Int {
    out_tail62.* = u64Secure() & state.RAND_MASK;
    return state.STATUS_OK;
}
