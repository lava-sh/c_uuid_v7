const std = @import("std");

const platform = @import("platform.zig");
const random = @import("random.zig");
const state = @import("state.zig");

fn ensureSeeded() state.Status {
    return random.ensureSeeded();
}

fn validateMode(mode: state.Mode) state.Status {
    return switch (mode) {
        .fast, .secure => .ok,
    };
}

fn validateNanos(nanos: u64) state.Status {
    if (nanos >= state.MAX_NANOS) {
        return .nanos_out_of_range;
    }
    return .ok;
}

fn buildTimestampMs(timestamp_s: u64, has_timestamp: bool, nanos: u64, has_nanos: bool, timestamp_ms: *u64) state.Status {
    if (!has_timestamp) {
        timestamp_ms.* = platform.nowMs();
        return .ok;
    }

    if (timestamp_s > state.MAX_TIMESTAMP_S) {
        return .timestamp_too_large;
    }

    var ms = timestamp_s * 1000;
    if (has_nanos) {
        ms += nanos / 1_000_000;
    }

    if (ms > state.MAX_TIMESTAMP_MS) {
        return .timestamp_too_large;
    }

    timestamp_ms.* = ms;
    return .ok;
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

fn advanceMonotonicStateSecure(observed_ms: u64, timestamp_ms: *u64, rand_a: *u16, tail62: *u64) state.Status {
    var counter = state.runtime.counter42;
    var current_ms = state.runtime.last_timestamp_ms;
    var increment: u64 = 0;
    var low32: u32 = 0;

    if (random.nextLow32AndIncrementSecure(&low32, &increment) != .ok) {
        return .random_failure;
    }

    if (observed_ms > current_ms) {
        current_ms = observed_ms;
        if (random.counter42Secure(&counter) != .ok) {
            return .random_failure;
        }
    } else {
        counter += increment;
        if (counter > state.MAX_COUNTER) {
            current_ms += 1;
            if (random.counter42Secure(&counter) != .ok) {
                return .random_failure;
            }
        }
    }

    state.runtime.last_timestamp_ms = current_ms;
    state.runtime.counter42 = counter;
    timestamp_ms.* = current_ms;
    random.splitCounter42(counter, low32, rand_a, tail62);
    return .ok;
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

pub fn buildDefault(mode: state.Mode, hi: *u64, lo: *u64) state.Status {
    var timestamp_ms: u64 = 0;
    var tail62: u64 = 0;
    var rand_a: u16 = 0;

    if (validateMode(mode) != .ok) {
        return .invalid_mode;
    }
    if (ensureSeeded() != .ok) {
        return .random_failure;
    }

    if (mode == .secure) {
        if (advanceMonotonicStateSecure(platform.nowMs(), &timestamp_ms, &rand_a, &tail62) != .ok) {
            return .random_failure;
        }
    } else {
        advanceMonotonicState(platform.nowMs(), &timestamp_ms, &rand_a, &tail62);
    }

    buildWords(timestamp_ms, rand_a, tail62, hi, lo);
    return .ok;
}

fn fillRandomBits(has_timestamp: bool, has_nanos: bool, nanos: u64, rand_a: *u16, tail62: *u64) state.Status {
    if (has_timestamp and has_nanos) {
        rand_a.* = @intCast(nanos & 0x0FFF);
        tail62.* = random.tail62();
        return .ok;
    }

    if (has_timestamp or has_nanos) {
        random.payload(rand_a, tail62);
        return .ok;
    }

    return @enumFromInt(1);
}

fn fillRandomBitsSecure(has_timestamp: bool, has_nanos: bool, nanos: u64, rand_a: *u16, tail62: *u64) state.Status {
    if (has_timestamp and has_nanos) {
        rand_a.* = @intCast(nanos & 0x0FFF);
        return random.tail62Secure(tail62);
    }

    if (has_timestamp or has_nanos) {
        return random.payloadSecure(rand_a, tail62);
    }

    return @enumFromInt(1);
}

pub fn buildParts(timestamp_s: u64, has_timestamp: state.Int, nanos: u64, has_nanos: state.Int, mode: state.Mode, hi: *u64, lo: *u64) state.Status {
    const have_timestamp = has_timestamp != 0;
    const have_nanos = has_nanos != 0;
    var timestamp_ms: u64 = 0;
    var tail62: u64 = 0;
    var rand_a: u16 = 0;

    if (validateMode(mode) != .ok) {
        return .invalid_mode;
    }
    if (ensureSeeded() != .ok) {
        return .random_failure;
    }
    if (have_nanos and validateNanos(nanos) != .ok) {
        return .nanos_out_of_range;
    }
    if (buildTimestampMs(timestamp_s, have_timestamp, nanos, have_nanos, &timestamp_ms) != .ok) {
        return .timestamp_too_large;
    }

    if (mode == .secure) {
        const random_state = fillRandomBitsSecure(have_timestamp, have_nanos, nanos, &rand_a, &tail62);
        if (random_state == .ok) {
            buildWords(timestamp_ms, rand_a, tail62, hi, lo);
            return .ok;
        }
        if (@intFromEnum(random_state) < 0) {
            return random_state;
        }
        if (advanceMonotonicStateSecure(timestamp_ms, &timestamp_ms, &rand_a, &tail62) != .ok) {
            return .random_failure;
        }
    } else {
        const random_state = fillRandomBits(have_timestamp, have_nanos, nanos, &rand_a, &tail62);
        if (random_state == .ok) {
            buildWords(timestamp_ms, rand_a, tail62, hi, lo);
            return .ok;
        }
        advanceMonotonicState(timestamp_ms, &timestamp_ms, &rand_a, &tail62);
    }

    buildWords(timestamp_ms, rand_a, tail62, hi, lo);
    return .ok;
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
