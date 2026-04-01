const std = @import("std");
const builtin = @import("builtin");

const platform = @import("platform.zig");
const state = @import("state.zig");

pub const UUIDWords = extern struct {
    hi: u64,
    lo: u64,
};

const Mode = enum(c_int) {
    fast = 0,
    secure = 1,
};

fn parseMode(mode: c_int) Mode {
    return if (mode == @intFromEnum(Mode.secure)) .secure else .fast;
}

fn randomU64Secure() u64 {
    var bytes: [8]u8 = undefined;
    std.crypto.random.bytes(&bytes);
    return std.mem.readInt(u64, &bytes, .big);
}

fn randomEnsureSeeded() bool {
    var seed: [24]u8 = undefined;

    if (state.runtime.prng_seeded) {
        return true;
    }
    if (platform.fillRandom(&seed, seed.len) != 0) {
        return false;
    }

    state.runtime.prng.seedWithBuf(seed);
    if (builtin.os.tag == .windows) {
        platform.platformSeeded();
    }
    state.runtime.prng_seeded = true;
    return true;
}

fn randomReseed() void {
    state.runtime.prng_seeded = false;
}

fn randomCounter42() u64 {
    return state.runtime.prng.next() & state.RESEED_MASK;
}

fn randomCounter42Secure(counter: *u64) void {
    counter.* = randomU64Secure() & state.RESEED_MASK;
}

fn randomSplitCounter42(counter: u64, low32: u32, rand_a: *u16, tail62: *u64) void {
    rand_a.* = @intCast(counter >> 30);
    tail62.* = ((counter & state.LOW30_MASK) << 32) | @as(u64, low32);
}

fn randomNextLow32AndIncrement(low32: *u32, increment: *u64) void {
    const random64 = state.runtime.prng.next();

    low32.* = @truncate(random64);
    increment.* = 1 + ((random64 >> 32) & 0x0F);
}

fn randomNextLow32AndIncrementSecure(low32: *u32, increment: *u64) void {
    const random64 = randomU64Secure();

    low32.* = @truncate(random64);
    increment.* = 1 + ((random64 >> 32) & 0x0F);
}

fn randomPayload(rand_a: *u16, tail62: *u64) void {
    randomSplitCounter42(randomCounter42(), @truncate(state.runtime.prng.next()), rand_a, tail62);
}

fn randomPayloadSecure(rand_a: *u16, tail62: *u64) void {
    var counter: u64 = 0;

    randomCounter42Secure(&counter);
    randomSplitCounter42(counter, @truncate(randomU64Secure()), rand_a, tail62);
}

fn randomTail62() u64 {
    return state.runtime.prng.next() & state.UUID_RAND_MASK;
}

fn randomTail62Secure(tail62: *u64) void {
    tail62.* = randomU64Secure() & state.UUID_RAND_MASK;
}

fn reseedGeneratorState() void {
    randomReseed();
    state.runtime.last_timestamp_ms = 0;
    state.runtime.counter42 = 0;
}

fn validateNanos(nanos: u64) c_int {
    if (nanos >= state.UUID_MAX_NANOS) {
        return 2;
    }
    return 0;
}

fn buildTimestampMs(
    timestamp_s: u64,
    has_timestamp: bool,
    nanos: u64,
    has_nanos: bool,
    timestamp_ms: *u64,
) c_int {
    if (!has_timestamp) {
        timestamp_ms.* = platform.nowMs();
        return 0;
    }
    if (timestamp_s > state.UUID_MAX_TIMESTAMP_S) {
        return 3;
    }

    var ms = timestamp_s * 1000;
    if (has_nanos) {
        ms += nanos / 1_000_000;
    }
    if (ms > state.UUID_MAX_TIMESTAMP_MS) {
        return 3;
    }

    timestamp_ms.* = ms;
    return 0;
}

fn advanceMonotonicState(observed_ms: u64, timestamp_ms: *u64, rand_a: *u16, tail62: *u64) void {
    var counter = state.runtime.counter42;
    var current_ms = state.runtime.last_timestamp_ms;
    var increment: u64 = 0;
    var low32: u32 = 0;

    randomNextLow32AndIncrement(&low32, &increment);
    if (observed_ms > current_ms) {
        current_ms = observed_ms;
        counter = randomCounter42();
    } else {
        counter += increment;
        if (counter > state.UUID_V7_MAX_COUNTER) {
            current_ms += 1;
            counter = randomCounter42();
        }
    }

    state.runtime.last_timestamp_ms = current_ms;
    state.runtime.counter42 = counter;
    timestamp_ms.* = current_ms;
    randomSplitCounter42(counter, low32, rand_a, tail62);
}

fn advanceMonotonicStateSecure(observed_ms: u64, timestamp_ms: *u64, rand_a: *u16, tail62: *u64) void {
    var counter = state.runtime.counter42;
    var current_ms = state.runtime.last_timestamp_ms;
    var increment: u64 = 0;
    var low32: u32 = 0;

    randomNextLow32AndIncrementSecure(&low32, &increment);
    if (observed_ms > current_ms) {
        current_ms = observed_ms;
        randomCounter42Secure(&counter);
    } else {
        counter += increment;
        if (counter > state.UUID_V7_MAX_COUNTER) {
            current_ms += 1;
            randomCounter42Secure(&counter);
        }
    }

    state.runtime.last_timestamp_ms = current_ms;
    state.runtime.counter42 = counter;
    timestamp_ms.* = current_ms;
    randomSplitCounter42(counter, low32, rand_a, tail62);
}

fn uuidBuildWords(timestamp_ms: u64, rand_a: u16, tail62: u64, hi: *u64, lo: *u64) void {
    hi.* = (timestamp_ms << state.UUID_TIMESTAMP_SHIFT) | state.UUID_VERSION_BITS | @as(u64, rand_a);
    lo.* = state.UUID_VARIANT_BITS | tail62;
}

fn buildUuid7Default(hi: *u64, lo: *u64) c_int {
    var timestamp_ms: u64 = 0;
    var tail62: u64 = 0;
    var rand_a: u16 = 0;

    if (!randomEnsureSeeded()) {
        return 1;
    }

    advanceMonotonicState(platform.nowMs(), &timestamp_ms, &rand_a, &tail62);
    uuidBuildWords(timestamp_ms, rand_a, tail62, hi, lo);
    return 0;
}

fn buildUuid7DefaultSecure(hi: *u64, lo: *u64) c_int {
    var timestamp_ms: u64 = 0;
    var tail62: u64 = 0;
    var rand_a: u16 = 0;

    if (!randomEnsureSeeded()) {
        return 1;
    }

    advanceMonotonicStateSecure(platform.nowMs(), &timestamp_ms, &rand_a, &tail62);
    uuidBuildWords(timestamp_ms, rand_a, tail62, hi, lo);
    return 0;
}

fn fillRandomBits(has_timestamp: bool, has_nanos: bool, nanos: u64, rand_a: *u16, tail62: *u64) c_int {
    if (has_timestamp and has_nanos) {
        rand_a.* = @intCast(nanos & 0x0FFF);
        tail62.* = randomTail62();
        return 0;
    }
    if (has_timestamp or has_nanos) {
        randomPayload(rand_a, tail62);
        return 0;
    }
    return 1;
}

fn fillUuid7RandomBitsSecure(has_timestamp: bool, has_nanos: bool, nanos: u64, rand_a: *u16, tail62: *u64) c_int {
    if (has_timestamp and has_nanos) {
        rand_a.* = @intCast(nanos & 0x0FFF);
        randomTail62Secure(tail62);
        return 0;
    }
    if (has_timestamp or has_nanos) {
        randomPayloadSecure(rand_a, tail62);
        return 0;
    }
    return 1;
}

fn buildUuid7WithParsedArgs(
    timestamp_ms: u64,
    has_timestamp: bool,
    nanos: u64,
    has_nanos: bool,
    hi: *u64,
    lo: *u64,
) c_int {
    var tail62: u64 = 0;
    var rand_a: u16 = 0;

    if (fillRandomBits(has_timestamp, has_nanos, nanos, &rand_a, &tail62) > 0) {
        var current_timestamp_ms = timestamp_ms;

        advanceMonotonicState(current_timestamp_ms, &current_timestamp_ms, &rand_a, &tail62);
        uuidBuildWords(current_timestamp_ms, rand_a, tail62, hi, lo);
        return 0;
    }

    uuidBuildWords(timestamp_ms, rand_a, tail62, hi, lo);
    return 0;
}

fn buildUuid7WithParsedArgsSecure(
    timestamp_ms: u64,
    has_timestamp: bool,
    nanos: u64,
    has_nanos: bool,
    hi: *u64,
    lo: *u64,
) c_int {
    var tail62: u64 = 0;
    var rand_a: u16 = 0;

    if (fillUuid7RandomBitsSecure(has_timestamp, has_nanos, nanos, &rand_a, &tail62) > 0) {
        var current_timestamp_ms = timestamp_ms;

        advanceMonotonicStateSecure(current_timestamp_ms, &current_timestamp_ms, &rand_a, &tail62);
        uuidBuildWords(current_timestamp_ms, rand_a, tail62, hi, lo);
        return 0;
    }

    uuidBuildWords(timestamp_ms, rand_a, tail62, hi, lo);
    return 0;
}

fn buildUuid7(
    timestamp_s: u64,
    has_timestamp: bool,
    nanos: u64,
    has_nanos: bool,
    mode: Mode,
    out: *UUIDWords,
) c_int {
    var timestamp_ms: u64 = 0;
    var status: c_int = 0;

    if (!randomEnsureSeeded()) {
        return 1;
    }
    if (has_nanos) {
        status = validateNanos(nanos);
        if (status != 0) {
            return status;
        }
    }
    status = buildTimestampMs(timestamp_s, has_timestamp, nanos, has_nanos, &timestamp_ms);
    if (status != 0) {
        return status;
    }
    if (mode == .secure and !has_timestamp and !has_nanos) {
        return buildUuid7DefaultSecure(&out.hi, &out.lo);
    }
    if (mode == .secure) {
        return buildUuid7WithParsedArgsSecure(
            timestamp_ms,
            has_timestamp,
            nanos,
            has_nanos,
            &out.hi,
            &out.lo,
        );
    }
    if (!has_timestamp and !has_nanos) {
        return buildUuid7Default(&out.hi, &out.lo);
    }
    return buildUuid7WithParsedArgs(
        timestamp_ms,
        has_timestamp,
        nanos,
        has_nanos,
        &out.hi,
        &out.lo,
    );
}

pub export fn c_uuid_v7_build(
    timestamp_s: u64,
    has_timestamp: u8,
    nanos: u64,
    has_nanos: u8,
    mode: c_int,
    out: *UUIDWords,
) c_int {
    return buildUuid7(
        timestamp_s,
        has_timestamp != 0,
        nanos,
        has_nanos != 0,
        parseMode(mode),
        out,
    );
}

pub export fn c_uuid_v7_reseed() void {
    reseedGeneratorState();
}
