const std = @import("std");
const builtin = @import("builtin");

const state = @import("state.zig");

const win = @cImport({
    @cDefine("WIN32_LEAN_AND_MEAN", "1");
    @cInclude("windows.h");
});

pub fn nowMs() u64 {
    if (state.runtime.query_interrupt_time_ptr) |query_interrupt_time| {
        var interrupt_time: u64 = 0;
        query_interrupt_time(&interrupt_time);
        return state.runtime.epoch_base_ms + @as(u64, interrupt_time / 10_000) - state.runtime.tick_base_ms;
    }

    return state.runtime.epoch_base_ms + @as(u64, win.GetTickCount64()) - state.runtime.tick_base_ms;
}

pub fn systemMs() u64 {
    var ft: win.FILETIME = undefined;

    win.GetSystemTimePreciseAsFileTime(&ft);
    const ticks = (@as(u64, ft.dwHighDateTime) << 32) | @as(u64, ft.dwLowDateTime);
    return (ticks - 116_444_736_000_000_000) / 10_000;
}

pub fn fillRandom(buf: [*]u8, len: isize) c_int {
    return if (std.os.windows.advapi32.RtlGenRandom(buf, @intCast(len)) != 0) 0 else -1;
}

pub fn platformSeeded() void {
    const kernel32 = win.GetModuleHandleA("kernel32.dll");

    state.runtime.epoch_base_ms = systemMs();
    state.runtime.query_interrupt_time_ptr = null;

    if (kernel32 != null) {
        state.runtime.query_interrupt_time_ptr = @ptrCast(win.GetProcAddress(kernel32, "QueryInterruptTime"));
    }

    if (state.runtime.query_interrupt_time_ptr) |query_interrupt_time| {
        var interrupt_time: u64 = 0;

        query_interrupt_time(&interrupt_time);
        state.runtime.tick_base_ms = @as(u64, interrupt_time / 10_000);
        return;
    }

    state.runtime.tick_base_ms = @as(u64, win.GetTickCount64());
}
