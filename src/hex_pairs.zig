const builtin = @import("builtin");

fn buildHexPairs() [256]u16 {
    var pairs: [256]u16 = undefined;

    for (0..256) |index| {
        const hi = "0123456789abcdef"[index >> 4];
        const lo = "0123456789abcdef"[index & 0x0F];

        if (builtin.cpu.arch.endian() == .big) {
            pairs[index] = (@as(u16, hi) << 8) | @as(u16, lo);
        } else {
            pairs[index] = @as(u16, hi) | (@as(u16, lo) << 8);
        }
    }

    return pairs;
}

const HEX_PAIRS = buildHexPairs();

pub fn hexPair(out: *u8, byte: u8) void {
    const pair = HEX_PAIRS[byte];
    @as(*align(1) u16, @ptrCast(out)).* = pair;
}
