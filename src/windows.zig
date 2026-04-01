const builtin = @import("builtin");
const state = @import("state.zig");

const c = @import("c.zig").c;

const win = @cImport({
    @cDefine("WIN32_LEAN_AND_MEAN", "1");
    @cInclude("windows.h");
    @cInclude("bcrypt.h");
});

fn ensureBcryptGenRandom() ?state.BCryptGenRandomFn {
    if (builtin.os.tag != .windows) {
        return null;
    }

    if (state.runtime.bcrypt_gen_random_ptr) |bcrypt_gen_random| {
        return bcrypt_gen_random;
    }

    const bcrypt_module = win.GetModuleHandleA("bcrypt.dll") orelse win.LoadLibraryA("bcrypt.dll");
    if (bcrypt_module == null) {
        return null;
    }

    state.runtime.bcrypt_gen_random_ptr = @ptrCast(win.GetProcAddress(bcrypt_module, "BCryptGenRandom"));
    return state.runtime.bcrypt_gen_random_ptr;
}

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
    const bcrypt_gen_random = ensureBcryptGenRandom() orelse return -1;
    const status = bcrypt_gen_random(null, buf, @intCast(len), win.BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return if (status >= 0) 0 else -1;
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
