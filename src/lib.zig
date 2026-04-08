const uuid7 = @import("uuid7.zig");
const state = @import("state.zig");

pub export fn uuid7_reseed() void {
    uuid7.reseed();
}

pub export fn uuid7_build_default(mode: state.Int, hi: *u64, lo: *u64) state.Int {
    return uuid7.buildDefault(mode, hi, lo);
}

pub export fn uuid7_build_parts(timestamp_s: u64, has_timestamp: state.Int, nanos: u64, has_nanos: state.Int, mode: state.Int, hi: *u64, lo: *u64) state.Int {
    return uuid7.buildParts(timestamp_s, has_timestamp, nanos, has_nanos, mode, hi, lo);
}

pub export fn uuid7_pack_bytes(hi: u64, lo: u64, bytes: *[16]u8) void {
    uuid7.packBytes(hi, lo, bytes);
}

pub export fn uuid7_pack_bytes_le(hi: u64, lo: u64, bytes: *[16]u8) void {
    uuid7.packBytesLe(hi, lo, bytes);
}

pub export fn uuid7_format_hex(hi: u64, lo: u64, out: *[32]u8) void {
    uuid7.formatHex(hi, lo, out);
}

pub export fn uuid7_format_hyphenated(hi: u64, lo: u64, out: *[36]u8) void {
    uuid7.formatHyphenated(hi, lo, out);
}

pub export fn uuid7_timestamp_ms(hi: u64) u64 {
    return uuid7.timestampMs(hi);
}

pub export fn uuid7_time_low(hi: u64) u32 {
    return uuid7.timeLow(hi);
}

pub export fn uuid7_time_mid(hi: u64) u16 {
    return uuid7.timeMid(hi);
}

pub export fn uuid7_time_hi_version(hi: u64) u16 {
    return uuid7.timeHiVersion(hi);
}

pub export fn uuid7_clock_seq_hi_variant(lo: u64) u8 {
    return uuid7.clockSeqHiVariant(lo);
}

pub export fn uuid7_clock_seq_low(lo: u64) u8 {
    return uuid7.clockSeqLow(lo);
}

pub export fn uuid7_clock_seq(lo: u64) u16 {
    return uuid7.clockSeq(lo);
}

pub export fn uuid7_node(lo: u64) u64 {
    return uuid7.node(lo);
}

pub export fn uuid7_compare(left_hi: u64, left_lo: u64, right_hi: u64, right_lo: u64) state.Int {
    return uuid7.compare(left_hi, left_lo, right_hi, right_lo);
}

pub export fn uuid7_hash(hi: u64, lo: u64) isize {
    return uuid7.hash(hi, lo);
}
