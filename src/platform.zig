const std = @import("std");
const builtin = @import("c.zig").builtin;
const state = @import("state.zig");

const c = @import("c.zig").c;
const windows = std.os.windows;
const kernel32 = windows.kernel32;

const has_timespec_layout = if (builtin.os.tag == .windows) false else @typeInfo(c.timespec) != .@"opaque";
const bcrypt_use_system_preferred_rng: u32 = 0x00000002;

const winapi = if (builtin.os.tag == .windows) struct {
    pub extern "kernel32" fn GetTickCount64() callconv(.winapi) u64;
    pub extern "kernel32" fn GetSystemTimePreciseAsFileTime(system_time_as_file_time: *windows.FILETIME) callconv(.winapi) void;
} else struct {};

fn ensureBcryptGenRandom() ?state.BCryptGenRandomFn {
    if (builtin.os.tag != .windows) {
        return null;
    }

    if (state.runtime.bcrypt_gen_random_ptr) |bcrypt_gen_random| {
        return bcrypt_gen_random;
    }

    const bcrypt_name = std.unicode.utf8ToUtf16LeStringLiteral("bcrypt.dll");
    const bcrypt_module = kernel32.GetModuleHandleW(bcrypt_name) orelse windows.LoadLibraryW(bcrypt_name) catch return null;

    state.runtime.bcrypt_gen_random_ptr = @ptrCast(@alignCast(kernel32.GetProcAddress(bcrypt_module, "BCryptGenRandom")));
    return state.runtime.bcrypt_gen_random_ptr;
}

pub fn nowMs() u64 {
    if (builtin.os.tag == .windows) {
        if (state.runtime.query_interrupt_time_ptr) |query_interrupt_time| {
            var interrupt_time: u64 = 0;
            query_interrupt_time(&interrupt_time);
            return state.runtime.epoch_base_ms + @as(u64, interrupt_time / 10_000) - state.runtime.tick_base_ms;
        }

        return state.runtime.epoch_base_ms + @as(u64, winapi.GetTickCount64()) - state.runtime.tick_base_ms;
    }

    return systemMs();
}

pub fn systemMs() u64 {
    if (builtin.os.tag == .windows) {
        var ft: windows.FILETIME = undefined;

        winapi.GetSystemTimePreciseAsFileTime(&ft);
        const ticks = (@as(u64, ft.dwHighDateTime) << 32) | @as(u64, ft.dwLowDateTime);
        return (ticks - 116_444_736_000_000_000) / 10_000;
    }

    if (@hasDecl(c, "CLOCK_REALTIME") and has_timespec_layout) {
        var ts: c.timespec = undefined;

        if (c.clock_gettime(c.CLOCK_REALTIME, &ts) == 0) {
            return @as(u64, @intCast(ts.tv_sec)) * 1000 + @as(u64, @intCast(ts.tv_nsec)) / 1_000_000;
        }
    }

    var tv: c.timeval = undefined;
    _ = c.gettimeofday(&tv, null);
    return @as(u64, @intCast(tv.tv_sec)) * 1000 + @as(u64, @intCast(tv.tv_usec)) / 1000;
}

pub fn fillRandom(buf: [*]u8, len: usize) state.Int {
    if (builtin.os.tag == .windows) {
        const bcrypt_gen_random = ensureBcryptGenRandom() orelse return state.STATUS_RANDOM_FAILURE;
        const status = bcrypt_gen_random(null, buf, @intCast(len), bcrypt_use_system_preferred_rng);
        return if (status >= 0) state.STATUS_OK else state.STATUS_RANDOM_FAILURE;
    }

    var offset: usize = 0;

    if (builtin.os.tag == .linux and @hasDecl(c, "getrandom")) {
        while (offset < len) {
            const rc = c.getrandom(buf + offset, len - offset, 0);
            if (rc < 0) {
                break;
            }
            offset += @intCast(rc);
        }
        if (offset == len) {
            return state.STATUS_OK;
        }
    }

    const fd = c.open("/dev/urandom", c.O_RDONLY);
    offset = 0;

    if (fd < 0) {
        return state.STATUS_RANDOM_FAILURE;
    }

    while (offset < len) {
        const rc = c.read(fd, buf + offset, len - offset);
        if (rc <= 0) {
            _ = c.close(fd);
            return state.STATUS_RANDOM_FAILURE;
        }
        offset += @intCast(rc);
    }

    _ = c.close(fd);
    return state.STATUS_OK;
}

pub fn platformSeeded() void {
    if (builtin.os.tag != .windows) {
        return;
    }

    const kernel32_name = std.unicode.utf8ToUtf16LeStringLiteral("kernel32.dll");
    const kernel32_module = kernel32.GetModuleHandleW(kernel32_name);

    state.runtime.epoch_base_ms = systemMs();
    state.runtime.query_interrupt_time_ptr = null;

    if (kernel32_module) |module| {
        state.runtime.query_interrupt_time_ptr = @ptrCast(@alignCast(kernel32.GetProcAddress(module, "QueryInterruptTime")));
    }

    if (state.runtime.query_interrupt_time_ptr) |query_interrupt_time| {
        var interrupt_time: u64 = 0;

        query_interrupt_time(&interrupt_time);
        state.runtime.tick_base_ms = @as(u64, interrupt_time / 10_000);
        return;
    }

    state.runtime.tick_base_ms = @as(u64, winapi.GetTickCount64());
}
