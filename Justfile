set windows-shell := ["pwsh.exe", "-NoLogo", "-Command"]

[private]
default:
    just --list

fmt-zig:
    & C:\Users\chiri\scoop\shims\zig.exe fmt src build.zig
