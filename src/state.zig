const RomuTrio = @import("romu_trio.zig");

const builtin = @import("builtin");
const c = @import("c.zig").c;

pub const UUIDObject = extern struct {
    ob_base: c.PyObject,
    hi: u64,
    lo: u64,
};

pub const UUID7Args = struct {
    timestamp_ms: u64 = 0,
    nanos: u64 = 0,
    has_timestamp: bool = false,
    has_nanos: bool = false,
};

pub const UUID_TIMESTAMP_SHIFT: u6 = 16;
pub const UUID_VERSION_BITS: u64 = 0x7000;
pub const UUID_VARIANT_BITS: u64 = 0x8000_0000_0000_0000;
pub const UUID_MAX_TIMESTAMP_MS: u64 = 0xFFFF_FFFF_FFFF;
pub const UUID_MAX_TIMESTAMP_S: u64 = UUID_MAX_TIMESTAMP_MS / 1000;
pub const UUID_MAX_NANOS: u64 = 1_000_000_000;
pub const UUID_V7_MAX_COUNTER: u64 = (1 << 42) - 1;
pub const UUID_MODE_FAST: c_int = 0;
pub const UUID_MODE_SECURE: c_int = 1;

pub const UUID_RAND_MASK: u64 = 0x3FFF_FFFF_FFFF_FFFF;
pub const RESEED_MASK: u64 = (1 << 41) - 1;
pub const LOW30_MASK: u64 = (1 << 30) - 1;

pub const QueryInterruptTimeFn = if (builtin.os.tag == .windows)
    *const fn (?*u64) callconv(.winapi) void
else
    *const fn (?*u64) callconv(.c) void;

pub const BCryptGenRandomFn = if (builtin.os.tag == .windows)
    *const fn (?*anyopaque, [*]u8, c_uint, c_uint) callconv(.winapi) c_int
else
    *const fn (?*anyopaque, [*]u8, c_uint, c_uint) callconv(.c) c_int;

pub const RuntimeState = struct {
    uuid_type: ?*c.PyTypeObject = null,
    reusable_uuid: ?*UUIDObject = null,
    last_timestamp_ms: u64 = 0,
    counter42: u64 = 0,
    prng: RomuTrio = undefined,
    prng_seeded: bool = false,
    epoch_base_ms: u64 = 0,
    tick_base_ms: u64 = 0,
    query_interrupt_time_ptr: ?QueryInterruptTimeFn = null,
    bcrypt_gen_random_ptr: ?BCryptGenRandomFn = null,
};

pub var runtime = RuntimeState{};
