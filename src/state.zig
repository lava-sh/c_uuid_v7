const RomuTrio = @import("romu_trio.zig");

pub const UUID_TIMESTAMP_SHIFT: u6 = 16;
pub const UUID_VERSION_BITS: u64 = 0x7000;
pub const UUID_VARIANT_BITS: u64 = 0x8000_0000_0000_0000;
pub const UUID_MAX_TIMESTAMP_MS: u64 = 0xFFFF_FFFF_FFFF;
pub const UUID_MAX_TIMESTAMP_S: u64 = UUID_MAX_TIMESTAMP_MS / 1000;
pub const UUID_MAX_NANOS: u64 = 1_000_000_000;
pub const UUID_V7_MAX_COUNTER: u64 = (1 << 42) - 1;

pub const UUID_RAND_MASK: u64 = 0x3FFF_FFFF_FFFF_FFFF;
pub const RESEED_MASK: u64 = (1 << 41) - 1;
pub const LOW30_MASK: u64 = (1 << 30) - 1;

pub const RuntimeState = struct {
    last_timestamp_ms: u64 = 0,
    counter42: u64 = 0,
    prng: RomuTrio = undefined,
    prng_seeded: bool = false,
    epoch_base_ms: u64 = 0,
    tick_base_ms: u64 = 0,
};

pub var runtime = RuntimeState{};
