// Local copy of `std.Random.RomuTrio` with only the pieces used by the fast UUIDv7 path.
// Differences from std: only direct seeding from a 24-byte buffer and direct `next()` are kept
// This avoids wrapper overhead on the hot path while keeping the stdlib algorithm.
const std = @import("std");

const RomuTrio = @This();

x_state: u64,
y_state: u64,
z_state: u64,

pub fn seedWithBuf(self: *RomuTrio, buf: [24]u8) void {
    const seed_buf = @as([3]u64, @bitCast(buf));
    self.x_state = seed_buf[0];
    self.y_state = seed_buf[1];
    self.z_state = seed_buf[2];
}

pub fn next(self: *RomuTrio) u64 {
    const xp = self.x_state;
    const yp = self.y_state;
    const zp = self.z_state;

    self.x_state = 15241094284759029579 *% zp;
    self.y_state = std.math.rotl(u64, yp -% xp, 12);
    self.z_state = std.math.rotl(u64, zp -% yp, 44);

    return xp;
}
