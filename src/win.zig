const builtin = @import("builtin");

pub const FILETIME = extern struct {
    dwLowDateTime: u32,
    dwHighDateTime: u32,
};

pub const HMODULE = ?*anyopaque;
pub const FARPROC = ?*anyopaque;
pub const BCRYPT_USE_SYSTEM_PREFERRED_RNG: u32 = 0x00000002;

pub extern "kernel32" fn GetModuleHandleA(module_name: ?[*:0]const u8) callconv(.winapi) HMODULE;
pub extern "kernel32" fn LoadLibraryA(file_name: [*:0]const u8) callconv(.winapi) HMODULE;
pub extern "kernel32" fn GetProcAddress(module: HMODULE, proc_name: [*:0]const u8) callconv(.winapi) FARPROC;
pub extern "kernel32" fn GetTickCount64() callconv(.winapi) u64;
pub extern "kernel32" fn GetSystemTimePreciseAsFileTime(system_time_as_file_time: *FILETIME) callconv(.winapi) void;

comptime {
    if (builtin.os.tag != .windows) {
        @compileError("win.zig is only valid on Windows targets");
    }
}
