const std = @import("std");
const RomuTrio = @import("romu_trio.zig");

pub const Int = c_int;

pub const Mode = enum(c_int) {
    fast = 0,
    secure = 1,
};

pub const Status = enum(c_int) {
    ok = 0,
    nanos_out_of_range = 1,
    timestamp_too_large = 2,
    random_failure = 3,
    invalid_mode = 4,
};

pub const TIMESTAMP_SHIFT: u6 = 16;
pub const VERSION_BITS: u64 = 0x7000;
pub const VARIANT_BITS: u64 = 0x8000_0000_0000_0000;
pub const MAX_TIMESTAMP_MS: u64 = 0xFFFF_FFFF_FFFF;
pub const MAX_TIMESTAMP_S: u64 = MAX_TIMESTAMP_MS / 1000;
pub const MAX_NANOS: u64 = 1_000_000_000;
pub const MAX_COUNTER: u64 = (1 << 42) - 1;

pub const RAND_MASK: u64 = 0x3FFF_FFFF_FFFF_FFFF;
pub const RESEED_MASK: u64 = (1 << 41) - 1;
pub const LOW30_MASK: u64 = (1 << 30) - 1;

pub const RuntimeState = struct {
    last_timestamp_ms: u64 = 0,
    counter42: u64 = 0,
    prng: RomuTrio = undefined,
    prng_seeded: bool = false,
};

pub var runtime = RuntimeState{};
