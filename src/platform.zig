const builtin = @import("c.zig").builtin;
const state = @import("state.zig");

const c = @import("c.zig").c;

fn ensureBcryptGenRandom() ?state.BCryptGenRandomFn {
    if (builtin.os.tag != .windows) {
        return null;
    }

    if (state.runtime.bcrypt_gen_random_ptr) |bcrypt_gen_random| {
        return bcrypt_gen_random;
    }

    const bcrypt_module = c.GetModuleHandleA("bcrypt.dll") orelse c.LoadLibraryA("bcrypt.dll");
    if (bcrypt_module == null) {
        return null;
    }

    state.runtime.bcrypt_gen_random_ptr = @ptrCast(c.GetProcAddress(bcrypt_module, "BCryptGenRandom"));
    return state.runtime.bcrypt_gen_random_ptr;
}

pub fn nowMs() u64 {
    if (builtin.os.tag == .windows) {
        if (state.runtime.query_interrupt_time_ptr) |query_interrupt_time| {
            var interrupt_time: c.ULONGLONG = 0;
            query_interrupt_time(&interrupt_time);
            return state.runtime.epoch_base_ms + @as(u64, interrupt_time / 10_000) - state.runtime.tick_base_ms;
        }

        return state.runtime.epoch_base_ms + @as(u64, c.GetTickCount64()) - state.runtime.tick_base_ms;
    }

    return systemMs();
}

pub fn systemMs() u64 {
    if (builtin.os.tag == .windows) {
        var ft: c.FILETIME = undefined;

        c.GetSystemTimePreciseAsFileTime(&ft);
        const ticks = (@as(u64, ft.dwHighDateTime) << 32) | @as(u64, ft.dwLowDateTime);
        return (ticks - 116_444_736_000_000_000) / 10_000;
    }

    if (@hasDecl(c, "CLOCK_REALTIME")) {
        var ts: c.timespec = undefined;

        if (c.clock_gettime(c.CLOCK_REALTIME, &ts) == 0) {
            return @as(u64, @intCast(ts.tv_sec)) * 1000 + @as(u64, @intCast(ts.tv_nsec)) / 1_000_000;
        }
    }

    var tv: c.timeval = undefined;
    _ = c.gettimeofday(&tv, null);
    return @as(u64, @intCast(tv.tv_sec)) * 1000 + @as(u64, @intCast(tv.tv_usec)) / 1000;
}

pub fn fillRandom(buf: [*]u8, len: c.Py_ssize_t) c_int {
    if (builtin.os.tag == .windows) {
        const bcrypt_gen_random = ensureBcryptGenRandom() orelse return -1;
        const status = bcrypt_gen_random(null, buf, @intCast(len), c.BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        return if (status >= 0) 0 else -1;
    }

    var offset: c.Py_ssize_t = 0;

    if (builtin.os.tag == .linux and @hasDecl(c, "getrandom")) {
        while (offset < len) {
            const rc = c.getrandom(buf + @as(usize, @intCast(offset)), @as(usize, @intCast(len - offset)), 0);
            if (rc < 0) {
                break;
            }
            offset += @intCast(rc);
        }
        if (offset == len) {
            return 0;
        }
    }

    const fd = c.open("/dev/urandom", c.O_RDONLY);
    offset = 0;

    if (fd < 0) {
        return -1;
    }

    while (offset < len) {
        const rc = c.read(fd, buf + @as(usize, @intCast(offset)), @as(usize, @intCast(len - offset)));
        if (rc <= 0) {
            _ = c.close(fd);
            return -1;
        }
        offset += @intCast(rc);
    }

    _ = c.close(fd);
    return 0;
}

pub fn platformSeeded() void {
    if (builtin.os.tag != .windows) {
        return;
    }

    const kernel32 = c.GetModuleHandleA("kernel32.dll");

    state.runtime.epoch_base_ms = systemMs();
    state.runtime.query_interrupt_time_ptr = null;

    if (kernel32 != null) {
        state.runtime.query_interrupt_time_ptr = @ptrCast(c.GetProcAddress(kernel32, "QueryInterruptTime"));
    }

    if (state.runtime.query_interrupt_time_ptr) |query_interrupt_time| {
        var interrupt_time: c.ULONGLONG = 0;

        query_interrupt_time(&interrupt_time);
        state.runtime.tick_base_ms = @as(u64, interrupt_time / 10_000);
        return;
    }

    state.runtime.tick_base_ms = @as(u64, c.GetTickCount64());
}
