const RomuTrio = @import("romu_trio.zig");

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

pub const RuntimeState = struct {
    last_timestamp_ms: u64 = 0,
    counter42: u64 = 0,
    prng: RomuTrio = undefined,
    prng_seeded: bool = false,
};

pub var runtime = RuntimeState{};
