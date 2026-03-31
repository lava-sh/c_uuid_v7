pub const builtin = @import("builtin");

pub const c = @cImport({
    @cDefine("PY_SSIZE_T_CLEAN", "1");
    @cInclude("Python.h");

    if (builtin.os.tag == .windows) {
        @cDefine("WIN32_LEAN_AND_MEAN", "1");
        @cInclude("windows.h");
        @cInclude("bcrypt.h");
    } else {
        @cInclude("fcntl.h");
        @cInclude("sys/time.h");
        @cInclude("time.h");
        @cInclude("unistd.h");

        if (builtin.os.tag == .linux) {
            @cInclude("sys/random.h");
        }
    }
});
