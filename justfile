set windows-shell := ["pwsh.exe", "-NoLogo", "-Command"]

[private]
default:
    @just --list

[script("pwsh")]
[doc("fmt *.c, *.h files")]
fmt-c:
    $files = Get-ChildItem src -Recurse -Include *.c, *.h | ForEach-Object { $_.FullName }
    if ($files.Count -gt 0) {
        clang-format -i $files
    }
