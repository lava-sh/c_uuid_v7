const std = @import("std");

const random = @import("random.zig");
const state = @import("state.zig");

const TIMESTAMP_SHIFT: u6 = 16;
const VERSION_BITS: u64 = 0x7000;
const VARIANT_BITS: u64 = 0x8000_0000_0000_0000;
const MAX_TIMESTAMP_MS: u64 = 0xFFFF_FFFF_FFFF;
const MAX_TIMESTAMP_S: u64 = MAX_TIMESTAMP_MS / 1000;
const MAX_NANOS: u64 = 1_000_000_000;
const MAX_COUNTER: u64 = (1 << 42) - 1;

fn validateMode(mode: state.Mode) !void {
    return switch (mode) {
        .fast, .secure => {},
    };
}

fn validateNanos(nanos: u64) !void {
    if (nanos >= MAX_NANOS) {
        return error.nanos_out_of_range;
    }
}

fn buildTimestampMs(timestamp_s: u64, has_timestamp: bool, nanos: u64, has_nanos: bool, timestamp_ms: *u64) !void {
    if (!has_timestamp) {
        timestamp_ms.* = random.nowMs();
        return;
    }

    if (timestamp_s > MAX_TIMESTAMP_S) {
        return error.timestamp_too_large;
    }

    var ms = timestamp_s * 1000;
    if (has_nanos) {
        ms += nanos / 1_000_000;
    }

    if (ms > MAX_TIMESTAMP_MS) {
        return error.timestamp_too_large;
    }

    timestamp_ms.* = ms;
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
        if (counter > MAX_COUNTER) {
            current_ms += 1;
            counter = random.counter42();
        }
    }

    state.runtime.last_timestamp_ms = current_ms;
    state.runtime.counter42 = counter;
    timestamp_ms.* = current_ms;
    random.splitCounter42(counter, low32, rand_a, tail62);
}

fn advanceMonotonicStateSecure(observed_ms: u64, timestamp_ms: *u64, rand_a: *u16, tail62: *u64) !void {
    var counter = state.runtime.counter42;
    var current_ms = state.runtime.last_timestamp_ms;
    var increment: u64 = 0;
    var low32: u32 = 0;

    try random.nextLow32AndIncrementSecure(&low32, &increment);

    if (observed_ms > current_ms) {
        current_ms = observed_ms;
        try random.counter42Secure(&counter);
    } else {
        counter += increment;
        if (counter > MAX_COUNTER) {
            current_ms += 1;
            try random.counter42Secure(&counter);
        }
    }

    state.runtime.last_timestamp_ms = current_ms;
    state.runtime.counter42 = counter;
    timestamp_ms.* = current_ms;
    random.splitCounter42(counter, low32, rand_a, tail62);
}

fn buildWords(timestamp_ms: u64, rand_a: u16, tail62: u64, hi: *u64, lo: *u64) void {
    hi.* = (timestamp_ms << TIMESTAMP_SHIFT) | VERSION_BITS | @as(u64, rand_a);
    lo.* = VARIANT_BITS | tail62;
}

pub fn reseed() void {
    random.reseed();
    state.runtime.last_timestamp_ms = 0;
    state.runtime.counter42 = 0;
}

pub fn buildDefault(mode: state.Mode, hi: *u64, lo: *u64) !void {
    var timestamp_ms: u64 = 0;
    var tail62: u64 = 0;
    var rand_a: u16 = 0;

    try validateMode(mode);
    try random.ensureSeeded();

    if (mode == .secure) {
        try advanceMonotonicStateSecure(random.nowMs(), &timestamp_ms, &rand_a, &tail62);
    } else {
        advanceMonotonicState(random.nowMs(), &timestamp_ms, &rand_a, &tail62);
    }

    buildWords(timestamp_ms, rand_a, tail62, hi, lo);
}

const FillResult = enum { random, fallback };

fn fillRandomBits(has_timestamp: bool, has_nanos: bool, nanos: u64, rand_a: *u16, tail62: *u64) FillResult {
    if (has_timestamp and has_nanos) {
        rand_a.* = @intCast(nanos & 0x0FFF);
        tail62.* = random.tail62();
        return .random;
    }

    if (has_timestamp or has_nanos) {
        random.payload(rand_a, tail62);
        return .random;
    }

    return .fallback;
}

fn fillRandomBitsSecure(has_timestamp: bool, has_nanos: bool, nanos: u64, rand_a: *u16, tail62: *u64) !FillResult {
    if (has_timestamp and has_nanos) {
        rand_a.* = @intCast(nanos & 0x0FFF);
        try random.tail62Secure(tail62);
        return .random;
    }

    if (has_timestamp or has_nanos) {
        try random.payloadSecure(rand_a, tail62);
        return .random;
    }

    return .fallback;
}

pub fn buildParts(timestamp_s: u64, has_timestamp: bool, nanos: u64, has_nanos: bool, mode: state.Mode, hi: *u64, lo: *u64) !void {
    var timestamp_ms: u64 = 0;
    var tail62: u64 = 0;
    var rand_a: u16 = 0;

    try validateMode(mode);
    try random.ensureSeeded();
    if (has_nanos) {
        try validateNanos(nanos);
    }
    try buildTimestampMs(timestamp_s, has_timestamp, nanos, has_nanos, &timestamp_ms);

    if (mode == .secure) {
        const result = try fillRandomBitsSecure(has_timestamp, has_nanos, nanos, &rand_a, &tail62);
        if (result == .random) {
            buildWords(timestamp_ms, rand_a, tail62, hi, lo);
            return;
        }
        try advanceMonotonicStateSecure(timestamp_ms, &timestamp_ms, &rand_a, &tail62);
    } else {
        const result = fillRandomBits(has_timestamp, has_nanos, nanos, &rand_a, &tail62);
        if (result == .random) {
            buildWords(timestamp_ms, rand_a, tail62, hi, lo);
            return;
        }
        advanceMonotonicState(timestamp_ms, &timestamp_ms, &rand_a, &tail62);
    }

    buildWords(timestamp_ms, rand_a, tail62, hi, lo);
}

pub fn packBytes(hi: u64, lo: u64, bytes: *[16]u8) void {
    std.mem.writeInt(u64, bytes[0..8], hi, .big);
    std.mem.writeInt(u64, bytes[8..16], lo, .big);
}

pub fn packBytesLe(hi: u64, lo: u64, bytes: *[16]u8) void {
    var be: [16]u8 = undefined;
    packBytes(hi, lo, &be);

    bytes[0] = be[3];
    bytes[1] = be[2];
    bytes[2] = be[1];
    bytes[3] = be[0];
    bytes[4] = be[5];
    bytes[5] = be[4];
    bytes[6] = be[7];
    bytes[7] = be[6];
    @memcpy(bytes[8..16], be[8..16]);
}

pub fn formatHex(hi: u64, lo: u64, out: *[32]u8) void {
    var bytes: [16]u8 = undefined;
    packBytes(hi, lo, &bytes);
    const hex = std.fmt.bytesToHex(bytes, .lower);
    @memcpy(out, &hex);
}

pub fn formatHyphenated(hi: u64, lo: u64, out: *[36]u8) void {
    var bytes: [16]u8 = undefined;
    packBytes(hi, lo, &bytes);
    const hex = std.fmt.bytesToHex(bytes, .lower);

    @memcpy(out[0..8], hex[0..8]);
    out[8] = '-';
    @memcpy(out[9..13], hex[8..12]);
    out[13] = '-';
    @memcpy(out[14..18], hex[12..16]);
    out[18] = '-';
    @memcpy(out[19..23], hex[16..20]);
    out[23] = '-';
    @memcpy(out[24..36], hex[20..32]);
}

pub fn timestampMs(hi: u64) u64 {
    return hi >> TIMESTAMP_SHIFT;
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

pub fn compare(left_hi: u64, left_lo: u64, right_hi: u64, right_lo: u64) c_int {
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
