const uuid = @import("uuid7.zig");
const state = @import("state.zig");

pub export fn reseed() void {
    uuid.reseed();
}

pub export fn build_default(mode: state.Mode, hi: *u64, lo: *u64) state.Status {
    return uuid.buildDefault(mode, hi, lo);
}

pub export fn build_parts(timestamp_s: u64, has_timestamp: state.Int, nanos: u64, has_nanos: state.Int, mode: state.Mode, hi: *u64, lo: *u64) state.Status {
    return uuid.buildParts(timestamp_s, has_timestamp, nanos, has_nanos, mode, hi, lo);
}

pub export fn pack_bytes(hi: u64, lo: u64, bytes: *[16]u8) void {
    uuid.packBytes(hi, lo, bytes);
}

pub export fn pack_bytes_le(hi: u64, lo: u64, bytes: *[16]u8) void {
    uuid.packBytesLe(hi, lo, bytes);
}

pub export fn format_hex(hi: u64, lo: u64, out: *[32]u8) void {
    uuid.formatHex(hi, lo, out);
}

pub export fn format_hyphenated(hi: u64, lo: u64, out: *[36]u8) void {
    uuid.formatHyphenated(hi, lo, out);
}

pub export fn timestamp_ms(hi: u64) u64 {
    return uuid.timestampMs(hi);
}

pub export fn time_low(hi: u64) u32 {
    return uuid.timeLow(hi);
}

pub export fn time_mid(hi: u64) u16 {
    return uuid.timeMid(hi);
}

pub export fn time_hi_version(hi: u64) u16 {
    return uuid.timeHiVersion(hi);
}

pub export fn clock_seq_hi_variant(lo: u64) u8 {
    return uuid.clockSeqHiVariant(lo);
}

pub export fn clock_seq_low(lo: u64) u8 {
    return uuid.clockSeqLow(lo);
}

pub export fn clock_seq(lo: u64) u16 {
    return uuid.clockSeq(lo);
}

pub export fn node(lo: u64) u64 {
    return uuid.node(lo);
}

pub export fn compare(left_hi: u64, left_lo: u64, right_hi: u64, right_lo: u64) state.Int {
    return uuid.compare(left_hi, left_lo, right_hi, right_lo);
}

pub export fn hash(hi: u64, lo: u64) isize {
    return uuid.hash(hi, lo);
}
