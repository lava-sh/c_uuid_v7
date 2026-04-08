const builtin = @import("builtin");

pub const posix = @cImport({
    @cInclude("time.h");
});

pub const linux = if (builtin.os.tag == .linux)
    @cImport({
        @cInclude("sys/random.h");
    })
else
    struct {};
