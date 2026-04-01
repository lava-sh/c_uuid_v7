const std = @import("std");

const state = @import("state.zig");
const windows = std.os.windows;

extern "kernel32" fn GetSystemTimePreciseAsFileTime(
    lpSystemTimeAsFileTime: *windows.FILETIME,
) callconv(.winapi) void;

fn readKsystemTime(ksystem_time: *const std.os.windows.KSYSTEM_TIME) u64 {
    while (true) {
        const high1 = ksystem_time.High1Time;
        const low = ksystem_time.LowPart;
        const high2 = ksystem_time.High2Time;

        if (high1 == high2) {
            const high_bits: u32 = @bitCast(high1);
            return (@as(u64, high_bits) << 32) | low;
        }
    }
}

fn interruptMs() u64 {
    return readKsystemTime(&std.os.windows.SharedUserData.InterruptTime) / 10_000;
}

pub fn nowMs() u64 {
    return state.runtime.epoch_base_ms + interruptMs() - state.runtime.tick_base_ms;
}

pub fn systemMs() u64 {
    var ft: windows.FILETIME = undefined;

    GetSystemTimePreciseAsFileTime(&ft);
    const ticks = (@as(u64, ft.dwHighDateTime) << 32) | @as(u64, ft.dwLowDateTime);
    return (ticks - 116_444_736_000_000_000) / 10_000;
}

pub fn fillRandom(buf: [*]u8, len: isize) c_int {
    windows.RtlGenRandom(buf[0..@intCast(len)]) catch return -1;
    return 0;
}

pub fn platformSeeded() void {
    state.runtime.epoch_base_ms = systemMs();
    state.runtime.tick_base_ms = interruptMs();
}
