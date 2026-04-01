const std = @import("std");
const builtin = @import("builtin");

const state = @import("state.zig");
const platform = @import("platform.zig");

const c = @import("c.zig").c;

fn unpackU64Be(bytes: *const [8]u8) u64 {
    return std.mem.readInt(u64, bytes, .big);
}

fn randomU64Secure() u64 {
    var bytes: [8]u8 = undefined;
    std.crypto.random.bytes(&bytes);
    return unpackU64Be(&bytes);
}

pub fn randomEnsureSeeded() c_int {
    var seed: [24]u8 = undefined;

    if (state.runtime.prng_seeded) {
        return 0;
    }

    if (platform.fillRandom(&seed, seed.len) != 0) {
        c.PyErr_SetString(state.pyExcOSError(), "unable to seed UUIDv7 generator");
        return -1;
    }

    state.runtime.prng.seedWithBuf(seed);

    if (builtin.os.tag == .windows) {
        platform.platformSeeded();
    }

    state.runtime.prng_seeded = true;
    return 0;
}

pub fn randomReseed() void {
    state.runtime.prng_seeded = false;
}

pub fn randomCounter42() u64 {
    return state.runtime.prng.next() & state.RESEED_MASK;
}

pub fn randomCounter42Secure(counter: *u64) c_int {
    counter.* = randomU64Secure() & state.RESEED_MASK;
    return 0;
}

pub fn randomSplitCounter42(counter: u64, low32: u32, rand_a: *u16, tail62: *u64) void {
    rand_a.* = @intCast(counter >> 30);
    tail62.* = ((counter & state.LOW30_MASK) << 32) | @as(u64, low32);
}

pub fn randomNextLow32AndIncrement(low32: *u32, increment: *u64) void {
    const random64 = state.runtime.prng.next();

    low32.* = @truncate(random64);
    increment.* = 1 + ((random64 >> 32) & 0x0F);
}

pub fn randomNextLow32AndIncrementSecure(low32: *u32, increment: *u64) c_int {
    const random64 = randomU64Secure();

    low32.* = @truncate(random64);
    increment.* = 1 + ((random64 >> 32) & 0x0F);
    return 0;
}

pub fn randomPayload(rand_a: *u16, tail62: *u64) void {
    randomSplitCounter42(randomCounter42(), @truncate(state.runtime.prng.next()), rand_a, tail62);
}

pub fn randomPayloadSecure(rand_a: *u16, tail62: *u64) c_int {
    var counter: u64 = 0;

    if (randomCounter42Secure(&counter) != 0) {
        return -1;
    }

    randomSplitCounter42(counter, @truncate(randomU64Secure()), rand_a, tail62);
    return 0;
}

pub fn randomTail62() u64 {
    return state.runtime.prng.next() & state.UUID_RAND_MASK;
}

pub fn randomTail62Secure(tail62: *u64) c_int {
    tail62.* = randomU64Secure() & state.UUID_RAND_MASK;
    return 0;
}
