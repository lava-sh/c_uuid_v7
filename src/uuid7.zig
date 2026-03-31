const std = @import("std");

const platform = @import("platform.zig");
const random = @import("random.zig");
const state = @import("state.zig");

const c = @import("c.zig").c;

pub fn reseedGeneratorState() void {
    random.randomReseed();
    state.runtime.last_timestamp_ms = 0;
    state.runtime.counter42 = 0;
}

pub fn uuidPackBytes(hi: u64, lo: u64, bytes: *[16]u8) void {
    std.mem.writeInt(u64, bytes[0..8], hi, .big);
    std.mem.writeInt(u64, bytes[8..16], lo, .big);
}

fn ensureSeeded() c_int {
    return random.randomEnsureSeeded();
}

fn parseU64(value: ?*c.PyObject, out: *u64, name: [*:0]const u8, none_object: ?*c.PyObject) c_int {
    if (value == null or value == none_object) {
        return 0;
    }

    const temp = c.PyLong_AsUnsignedLongLong(value);
    if (c.PyErr_Occurred() != null) {
        _ = c.PyErr_Format(c.PyExc_TypeError, "%s must be a non-negative int or None", name);
        return -1;
    }

    out.* = temp;
    return 1;
}

fn validateNanos(nanos: u64) c_int {
    if (nanos >= state.UUID_MAX_NANOS) {
        c.PyErr_SetString(c.PyExc_ValueError, "nanos must be in range 0..999999999");
        return -1;
    }
    return 0;
}

fn buildTimestampMs(timestamp_s: u64, has_timestamp: bool, nanos: u64, has_nanos: bool, timestamp_ms: *u64) c_int {
    if (!has_timestamp) {
        timestamp_ms.* = platform.nowMs();
        return 0;
    }

    if (timestamp_s > state.UUID_MAX_TIMESTAMP_S) {
        c.PyErr_SetString(c.PyExc_ValueError, "timestamp is too large");
        return -1;
    }

    var ms = timestamp_s * 1000;
    if (has_nanos) {
        ms += nanos / 1_000_000;
    }

    if (ms > state.UUID_MAX_TIMESTAMP_MS) {
        c.PyErr_SetString(c.PyExc_ValueError, "timestamp is too large");
        return -1;
    }

    timestamp_ms.* = ms;
    return 0;
}

fn parseArgs(timestamp_obj: ?*c.PyObject, nanos_obj: ?*c.PyObject, none_object: ?*c.PyObject, parsed: *state.UUID7Args) c_int {
    var timestamp_s: u64 = 0;

    parsed.* = .{};

    const has_timestamp = parseU64(timestamp_obj, &timestamp_s, "timestamp", none_object);
    if (has_timestamp < 0) {
        return -1;
    }
    parsed.has_timestamp = has_timestamp != 0;

    const has_nanos = parseU64(nanos_obj, &parsed.nanos, "nanos", none_object);
    if (has_nanos < 0) {
        return -1;
    }
    parsed.has_nanos = has_nanos != 0;

    if (parsed.has_nanos and validateNanos(parsed.nanos) != 0) {
        return -1;
    }

    return buildTimestampMs(timestamp_s, parsed.has_timestamp, parsed.nanos, parsed.has_nanos, &parsed.timestamp_ms);
}

fn advanceMonotonicState(observed_ms: u64, timestamp_ms: *u64, rand_a: *u16, tail62: *u64) void {
    var counter = state.runtime.counter42;
    var current_ms = state.runtime.last_timestamp_ms;
    var increment: u64 = 0;
    var low32: u32 = 0;

    random.randomNextLow32AndIncrement(&low32, &increment);

    if (observed_ms > current_ms) {
        current_ms = observed_ms;
        counter = random.randomCounter42();
    } else {
        counter += increment;
        if (counter > state.UUID_V7_MAX_COUNTER) {
            current_ms += 1;
            counter = random.randomCounter42();
        }
    }

    state.runtime.last_timestamp_ms = current_ms;
    state.runtime.counter42 = counter;
    timestamp_ms.* = current_ms;
    random.randomSplitCounter42(counter, low32, rand_a, tail62);
}

fn advanceMonotonicStateSecure(observed_ms: u64, timestamp_ms: *u64, rand_a: *u16, tail62: *u64) c_int {
    var counter = state.runtime.counter42;
    var current_ms = state.runtime.last_timestamp_ms;
    var increment: u64 = 0;
    var low32: u32 = 0;

    if (random.randomNextLow32AndIncrementSecure(&low32, &increment) != 0) {
        return -1;
    }

    if (observed_ms > current_ms) {
        current_ms = observed_ms;
        if (random.randomCounter42Secure(&counter) != 0) {
            return -1;
        }
    } else {
        counter += increment;
        if (counter > state.UUID_V7_MAX_COUNTER) {
            current_ms += 1;
            if (random.randomCounter42Secure(&counter) != 0) {
                return -1;
            }
        }
    }

    state.runtime.last_timestamp_ms = current_ms;
    state.runtime.counter42 = counter;
    timestamp_ms.* = current_ms;
    random.randomSplitCounter42(counter, low32, rand_a, tail62);
    return 0;
}

fn uuidBuildWords(timestamp_ms: u64, rand_a: u16, tail62: u64, hi: *u64, lo: *u64) void {
    hi.* = (timestamp_ms << state.UUID_TIMESTAMP_SHIFT) | state.UUID_VERSION_BITS | @as(u64, rand_a);
    lo.* = state.UUID_VARIANT_BITS | tail62;
}

pub fn buildUuid7Default(hi: *u64, lo: *u64) c_int {
    var timestamp_ms: u64 = 0;
    var tail62: u64 = 0;
    var rand_a: u16 = 0;

    if (ensureSeeded() != 0) {
        return -1;
    }

    advanceMonotonicState(platform.nowMs(), &timestamp_ms, &rand_a, &tail62);
    uuidBuildWords(timestamp_ms, rand_a, tail62, hi, lo);
    return 0;
}

pub fn buildUuid7DefaultSecure(hi: *u64, lo: *u64) c_int {
    var timestamp_ms: u64 = 0;
    var tail62: u64 = 0;
    var rand_a: u16 = 0;

    if (ensureSeeded() != 0) {
        return -1;
    }

    if (advanceMonotonicStateSecure(platform.nowMs(), &timestamp_ms, &rand_a, &tail62) != 0) {
        return -1;
    }

    uuidBuildWords(timestamp_ms, rand_a, tail62, hi, lo);
    return 0;
}

fn fillRandomBits(args: *const state.UUID7Args, rand_a: *u16, tail62: *u64) c_int {
    if (args.has_timestamp and args.has_nanos) {
        rand_a.* = @intCast(args.nanos & 0x0FFF);
        tail62.* = random.randomTail62();
        return 0;
    }

    if (args.has_timestamp or args.has_nanos) {
        random.randomPayload(rand_a, tail62);
        return 0;
    }

    return 1;
}

fn fillUuid7RandomBitsSecure(args: *const state.UUID7Args, rand_a: *u16, tail62: *u64) c_int {
    if (args.has_timestamp and args.has_nanos) {
        rand_a.* = @intCast(args.nanos & 0x0FFF);
        return random.randomTail62Secure(tail62);
    }

    if (args.has_timestamp or args.has_nanos) {
        return random.randomPayloadSecure(rand_a, tail62);
    }

    return 1;
}

fn buildUuid7WithParsedArgs(args: *const state.UUID7Args, hi: *u64, lo: *u64) c_int {
    var tail62: u64 = 0;
    var rand_a: u16 = 0;

    const random_state = fillRandomBits(args, &rand_a, &tail62);
    if (random_state > 0) {
        var timestamp_ms = args.timestamp_ms;

        advanceMonotonicState(timestamp_ms, &timestamp_ms, &rand_a, &tail62);
        uuidBuildWords(timestamp_ms, rand_a, tail62, hi, lo);
        return 0;
    }

    uuidBuildWords(args.timestamp_ms, rand_a, tail62, hi, lo);
    return 0;
}

fn buildUuid7WithParsedArgsSecure(args: *const state.UUID7Args, hi: *u64, lo: *u64) c_int {
    var tail62: u64 = 0;
    var rand_a: u16 = 0;

    const random_state = fillUuid7RandomBitsSecure(args, &rand_a, &tail62);

    if (random_state < 0) {
        return -1;
    }
    if (random_state > 0) {
        var timestamp_ms = args.timestamp_ms;

        if (advanceMonotonicStateSecure(timestamp_ms, &timestamp_ms, &rand_a, &tail62) != 0) {
            return -1;
        }
        uuidBuildWords(timestamp_ms, rand_a, tail62, hi, lo);
        return 0;
    }

    uuidBuildWords(args.timestamp_ms, rand_a, tail62, hi, lo);
    return 0;
}

pub fn buildUuid7PartsFromArgs(timestamp_obj: ?*c.PyObject, nanos_obj: ?*c.PyObject, mode: c_int, none_object: ?*c.PyObject, hi: *u64, lo: *u64) c_int {
    var parsed = state.UUID7Args{};

    if (ensureSeeded() != 0) {
        return -1;
    }

    if (parseArgs(timestamp_obj, nanos_obj, none_object, &parsed) != 0) {
        return -1;
    }

    if (mode == state.UUID_MODE_SECURE) {
        return buildUuid7WithParsedArgsSecure(&parsed, hi, lo);
    }

    return buildUuid7WithParsedArgs(&parsed, hi, lo);
}

pub fn parseMode(value: ?*c.PyObject, none_object: ?*c.PyObject, mode: *c_int) c_int {
    if (value == null or value == none_object) {
        mode.* = state.UUID_MODE_FAST;
        return 0;
    }

    if (c.PyUnicode_Check(value) == 0) {
        c.PyErr_SetString(c.PyExc_TypeError, "mode must be 'fast', 'secure', or None");
        return -1;
    }

    if (c.PyUnicode_CompareWithASCIIString(value, "fast") == 0) {
        mode.* = state.UUID_MODE_FAST;
        return 0;
    }

    if (c.PyUnicode_CompareWithASCIIString(value, "secure") == 0) {
        mode.* = state.UUID_MODE_SECURE;
        return 0;
    }

    c.PyErr_SetString(c.PyExc_ValueError, "mode must be 'fast' or 'secure'");
    return -1;
}
