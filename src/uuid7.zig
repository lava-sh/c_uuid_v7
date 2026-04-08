const std = @import("std");

const hexpairs = @import("hexpairs.zig");
const platform = @import("platform.zig");
const random = @import("random.zig");
const state = @import("state.zig");

fn ensureSeeded() state.Int {
    return random.ensureSeeded();
}

fn validateMode(mode: state.Int) state.Int {
    if (mode == state.MODE_FAST or mode == state.MODE_SECURE) {
        return state.STATUS_OK;
    }
    return state.STATUS_INVALID_MODE;
}

fn validateNanos(nanos: u64) state.Int {
    if (nanos >= state.MAX_NANOS) {
        return state.STATUS_NANOS_OUT_OF_RANGE;
    }
    return state.STATUS_OK;
}

fn buildTimestampMs(timestamp_s: u64, has_timestamp: bool, nanos: u64, has_nanos: bool, timestamp_ms: *u64) state.Int {
    if (!has_timestamp) {
        timestamp_ms.* = platform.nowMs();
        return state.STATUS_OK;
    }

    if (timestamp_s > state.MAX_TIMESTAMP_S) {
        return state.STATUS_TIMESTAMP_TOO_LARGE;
    }

    var ms = timestamp_s * 1000;
    if (has_nanos) {
        ms += nanos / 1_000_000;
    }

    if (ms > state.MAX_TIMESTAMP_MS) {
        return state.STATUS_TIMESTAMP_TOO_LARGE;
    }

    timestamp_ms.* = ms;
    return state.STATUS_OK;
}

fn advanceMonotonicState(observed_ms: u64, timestamp_ms: *u64, rand_a: *u16, tail62: *u64) void {
    var counter = state.runtime.counter42;
    var current_ms = state.runtime.last_timestamp_ms;
    var increment: u64 = 0;
    var low32: u32 = 0;

    random.nextLow32AndIncrement(&low32, &increment);

    if (observed_ms > current_ms) {
        current_ms = observed_ms;
        counter = random.counter42();
    } else {
        counter += increment;
        if (counter > state.MAX_COUNTER) {
            current_ms += 1;
            counter = random.counter42();
        }
    }

    state.runtime.last_timestamp_ms = current_ms;
    state.runtime.counter42 = counter;
    timestamp_ms.* = current_ms;
    random.splitCounter42(counter, low32, rand_a, tail62);
}

fn advanceMonotonicStateSecure(observed_ms: u64, timestamp_ms: *u64, rand_a: *u16, tail62: *u64) state.Int {
    var counter = state.runtime.counter42;
    var current_ms = state.runtime.last_timestamp_ms;
    var increment: u64 = 0;
    var low32: u32 = 0;

    if (random.nextLow32AndIncrementSecure(&low32, &increment) != state.STATUS_OK) {
        return state.STATUS_RANDOM_FAILURE;
    }

    if (observed_ms > current_ms) {
        current_ms = observed_ms;
        if (random.counter42Secure(&counter) != state.STATUS_OK) {
            return state.STATUS_RANDOM_FAILURE;
        }
    } else {
        counter += increment;
        if (counter > state.MAX_COUNTER) {
            current_ms += 1;
            if (random.counter42Secure(&counter) != state.STATUS_OK) {
                return state.STATUS_RANDOM_FAILURE;
            }
        }
    }

    state.runtime.last_timestamp_ms = current_ms;
    state.runtime.counter42 = counter;
    timestamp_ms.* = current_ms;
    random.splitCounter42(counter, low32, rand_a, tail62);
    return state.STATUS_OK;
}

fn buildWords(timestamp_ms: u64, rand_a: u16, tail62: u64, hi: *u64, lo: *u64) void {
    hi.* = (timestamp_ms << state.TIMESTAMP_SHIFT) | state.VERSION_BITS | @as(u64, rand_a);
    lo.* = state.VARIANT_BITS | tail62;
}

pub fn reseed() void {
    random.reseed();
    state.runtime.last_timestamp_ms = 0;
    state.runtime.counter42 = 0;
}

pub fn buildDefault(mode: state.Int, hi: *u64, lo: *u64) state.Int {
    var timestamp_ms: u64 = 0;
    var tail62: u64 = 0;
    var rand_a: u16 = 0;

    if (validateMode(mode) != state.STATUS_OK) {
        return state.STATUS_INVALID_MODE;
    }
    if (ensureSeeded() != state.STATUS_OK) {
        return state.STATUS_RANDOM_FAILURE;
    }

    if (mode == state.MODE_SECURE) {
        if (advanceMonotonicStateSecure(platform.nowMs(), &timestamp_ms, &rand_a, &tail62) != state.STATUS_OK) {
            return state.STATUS_RANDOM_FAILURE;
        }
    } else {
        advanceMonotonicState(platform.nowMs(), &timestamp_ms, &rand_a, &tail62);
    }

    buildWords(timestamp_ms, rand_a, tail62, hi, lo);
    return state.STATUS_OK;
}

fn fillRandomBits(has_timestamp: bool, has_nanos: bool, nanos: u64, rand_a: *u16, tail62: *u64) state.Int {
    if (has_timestamp and has_nanos) {
        rand_a.* = @intCast(nanos & 0x0FFF);
        tail62.* = random.tail62();
        return state.STATUS_OK;
    }

    if (has_timestamp or has_nanos) {
        random.payload(rand_a, tail62);
        return state.STATUS_OK;
    }

    return 1;
}

fn fillRandomBitsSecure(has_timestamp: bool, has_nanos: bool, nanos: u64, rand_a: *u16, tail62: *u64) state.Int {
    if (has_timestamp and has_nanos) {
        rand_a.* = @intCast(nanos & 0x0FFF);
        return random.tail62Secure(tail62);
    }

    if (has_timestamp or has_nanos) {
        return random.payloadSecure(rand_a, tail62);
    }

    return 1;
}

pub fn buildParts(timestamp_s: u64, has_timestamp: state.Int, nanos: u64, has_nanos: state.Int, mode: state.Int, hi: *u64, lo: *u64) state.Int {
    const have_timestamp = has_timestamp != 0;
    const have_nanos = has_nanos != 0;
    var timestamp_ms: u64 = 0;
    var tail62: u64 = 0;
    var rand_a: u16 = 0;

    if (validateMode(mode) != state.STATUS_OK) {
        return state.STATUS_INVALID_MODE;
    }
    if (ensureSeeded() != state.STATUS_OK) {
        return state.STATUS_RANDOM_FAILURE;
    }
    if (have_nanos and validateNanos(nanos) != state.STATUS_OK) {
        return state.STATUS_NANOS_OUT_OF_RANGE;
    }
    if (buildTimestampMs(timestamp_s, have_timestamp, nanos, have_nanos, &timestamp_ms) != state.STATUS_OK) {
        return state.STATUS_TIMESTAMP_TOO_LARGE;
    }

    if (mode == state.MODE_SECURE) {
        const random_state = fillRandomBitsSecure(have_timestamp, have_nanos, nanos, &rand_a, &tail62);
        if (random_state == state.STATUS_OK) {
            buildWords(timestamp_ms, rand_a, tail62, hi, lo);
            return state.STATUS_OK;
        }
        if (random_state < 0) {
            return random_state;
        }
        if (advanceMonotonicStateSecure(timestamp_ms, &timestamp_ms, &rand_a, &tail62) != state.STATUS_OK) {
            return state.STATUS_RANDOM_FAILURE;
        }
    } else {
        const random_state = fillRandomBits(have_timestamp, have_nanos, nanos, &rand_a, &tail62);
        if (random_state == state.STATUS_OK) {
            buildWords(timestamp_ms, rand_a, tail62, hi, lo);
            return state.STATUS_OK;
        }
        advanceMonotonicState(timestamp_ms, &timestamp_ms, &rand_a, &tail62);
    }

    buildWords(timestamp_ms, rand_a, tail62, hi, lo);
    return state.STATUS_OK;
}

pub fn packBytes(hi: u64, lo: u64, bytes: *[16]u8) void {
    std.mem.writeInt(u64, bytes[0..8], hi, .big);
    std.mem.writeInt(u64, bytes[8..16], lo, .big);
}

pub fn packBytesLe(hi: u64, lo: u64, bytes: *[16]u8) void {
    var reordered: [16]u8 = undefined;
    var big_endian: [16]u8 = undefined;

    packBytes(hi, lo, &big_endian);
    reordered[0] = big_endian[3];
    reordered[1] = big_endian[2];
    reordered[2] = big_endian[1];
    reordered[3] = big_endian[0];
    reordered[4] = big_endian[5];
    reordered[5] = big_endian[4];
    reordered[6] = big_endian[7];
    reordered[7] = big_endian[6];
    @memcpy(reordered[8..16], big_endian[8..16]);
    bytes.* = reordered;
}

pub fn formatHex(hi: u64, lo: u64, out: *[32]u8) void {
    var bytes: [16]u8 = undefined;
    var j: usize = 0;

    packBytes(hi, lo, &bytes);
    for (bytes) |byte| {
        hexpairs.hexPair(&out[j], byte);
        j += 2;
    }
}

pub fn formatHyphenated(hi: u64, lo: u64, out: *[36]u8) void {
    var bytes: [16]u8 = undefined;
    var j: usize = 0;

    packBytes(hi, lo, &bytes);
    for (bytes, 0..) |byte, index| {
        if (index == 4 or index == 6 or index == 8 or index == 10) {
            out[j] = '-';
            j += 1;
        }
        hexpairs.hexPair(&out[j], byte);
        j += 2;
    }
}

pub fn timestampMs(hi: u64) u64 {
    return hi >> state.TIMESTAMP_SHIFT;
}

pub fn timeLow(hi: u64) u32 {
    return @truncate(hi >> 32);
}

pub fn timeMid(hi: u64) u16 {
    return @truncate((hi >> 16) & 0xFFFF);
}

pub fn timeHiVersion(hi: u64) u16 {
    return @truncate(hi & 0xFFFF);
}

pub fn clockSeqHiVariant(lo: u64) u8 {
    return @truncate(lo >> 56);
}

pub fn clockSeqLow(lo: u64) u8 {
    return @truncate((lo >> 48) & 0xFF);
}

pub fn clockSeq(lo: u64) u16 {
    const high: u16 = @truncate((lo >> 56) & 0x3F);
    const low: u16 = @truncate((lo >> 48) & 0xFF);
    return (high << 8) | low;
}

pub fn node(lo: u64) u64 {
    return lo & 0xFFFF_FFFF_FFFF;
}

pub fn compare(left_hi: u64, left_lo: u64, right_hi: u64, right_lo: u64) state.Int {
    if (left_hi != right_hi) {
        return if (left_hi < right_hi) -1 else 1;
    }
    if (left_lo != right_lo) {
        return if (left_lo < right_lo) -1 else 1;
    }
    return 0;
}

pub fn hash(hi: u64, lo: u64) isize {
    const mixed = hi ^ (hi >> 32) ^ lo ^ (lo >> 32);
    var result: isize = if (@bitSizeOf(isize) == 64)
        @bitCast(mixed)
    else
        @bitCast(@as(u32, @truncate(mixed ^ (mixed >> 32))));

    if (result == -1) {
        result = -2;
    }

    return result;
}
