pub const builtin = @import("builtin");

pub const c = @cImport({
    @cDefine("PY_SSIZE_T_CLEAN", "1");
    @cInclude("Python.h");
    @cInclude("_python.h");

    if (builtin.os.tag != .windows) {
        @cInclude("fcntl.h");
        @cInclude("sys/time.h");
        @cInclude("time.h");
        @cInclude("unistd.h");

        if (builtin.os.tag == .linux) {
            @cInclude("sys/random.h");
        }
    }
});
