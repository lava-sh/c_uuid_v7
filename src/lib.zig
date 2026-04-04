const uuid7 = @import("uuid7.zig");
const state = @import("state.zig");

pub export fn c_uuid_v7_reseed() void {
    uuid7.reseedGeneratorState();
}

pub export fn c_uuid_v7_build_default(mode: state.Int, hi: *u64, lo: *u64) state.Int {
    return uuid7.buildUuid7Default(mode, hi, lo);
}

pub export fn c_uuid_v7_build_parts(timestamp_s: u64, has_timestamp: state.Int, nanos: u64, has_nanos: state.Int, mode: state.Int, hi: *u64, lo: *u64) state.Int {
    return uuid7.buildUuid7Parts(timestamp_s, has_timestamp, nanos, has_nanos, mode, hi, lo);
}

pub export fn c_uuid_v7_pack_bytes(hi: u64, lo: u64, bytes: *[16]u8) void {
    uuid7.uuidPackBytes(hi, lo, bytes);
}

pub export fn c_uuid_v7_pack_bytes_le(hi: u64, lo: u64, bytes: *[16]u8) void {
    uuid7.uuidPackBytesLe(hi, lo, bytes);
}

pub export fn c_uuid_v7_format_hex(hi: u64, lo: u64, out: *[32]u8) void {
    uuid7.uuidFormatHex(hi, lo, out);
}

pub export fn c_uuid_v7_format_hyphenated(hi: u64, lo: u64, out: *[36]u8) void {
    uuid7.uuidFormatHyphenated(hi, lo, out);
}

pub export fn c_uuid_v7_timestamp_ms(hi: u64) u64 {
    return uuid7.uuidTimestampMs(hi);
}

pub export fn c_uuid_v7_time_low(hi: u64) u32 {
    return uuid7.uuidTimeLow(hi);
}

pub export fn c_uuid_v7_time_mid(hi: u64) u16 {
    return uuid7.uuidTimeMid(hi);
}

pub export fn c_uuid_v7_time_hi_version(hi: u64) u16 {
    return uuid7.uuidTimeHiVersion(hi);
}

pub export fn c_uuid_v7_clock_seq_hi_variant(lo: u64) u8 {
    return uuid7.uuidClockSeqHiVariant(lo);
}

pub export fn c_uuid_v7_clock_seq_low(lo: u64) u8 {
    return uuid7.uuidClockSeqLow(lo);
}

pub export fn c_uuid_v7_clock_seq(lo: u64) u16 {
    return uuid7.uuidClockSeq(lo);
}

pub export fn c_uuid_v7_node(lo: u64) u64 {
    return uuid7.uuidNode(lo);
}

pub export fn c_uuid_v7_compare(left_hi: u64, left_lo: u64, right_hi: u64, right_lo: u64) state.Int {
    return uuid7.uuidCompare(left_hi, left_lo, right_hi, right_lo);
}

pub export fn c_uuid_v7_hash(hi: u64, lo: u64) isize {
    return uuid7.uuidHash(hi, lo);
}
