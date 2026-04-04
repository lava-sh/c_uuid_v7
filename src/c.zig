pub const builtin = @import("builtin");

pub const c = if (builtin.os.tag == .windows)
    struct {}
else
    @cImport({
        @cInclude("fcntl.h");
        @cInclude("sys/time.h");
        @cInclude("time.h");
        @cInclude("unistd.h");

        if (builtin.os.tag == .linux) {
            @cInclude("sys/random.h");
        }
    });
