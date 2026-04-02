set windows-shell := ["pwsh.exe", "-NoLogo", "-Command"]

[private]
default:
    @just --list

[doc("fmt *.c, *.h files")]
[script("pwsh")]
fmt-c:
    $files = Get-ChildItem src -Recurse -Include *.c, *.h | ForEach-Object { $_.FullName }
    if ($files.Count -gt 0) {
        $formatConfig = Join-Path $PWD ".clang-format"
        clang-format -i --style="file:$formatConfig" $files
    }

[doc("run all clang-based C checks")]
[script("pwsh")]
lint-c:
    $python = Join-Path $PWD ".venv\Scripts\python.exe"
    $python_include = & $python -c "import sysconfig; print(sysconfig.get_path('include'))"
    $python_plat_include = & $python -c "import sysconfig; print(sysconfig.get_path('platinclude'))"
    $tidy_config = Join-Path $PWD ".clang-tidy"
    if (-not $python_include) {
        throw "Unable to detect Python include path"
    }

    function Invoke-FilteredClangTool {
        param(
            [string]$Tool,
            [string[]]$Arguments
        )

        $output = & $Tool @Arguments 2>&1
        $exit_code = $LASTEXITCODE

        foreach ($line in $output) {
            if (
                $line -notmatch '^\d+ warnings generated\.$' -and
                $line -notmatch '^Suppressed \d+ warnings' -and
                $line -notmatch '^Use -header-filter=.*' -and
                $line -notmatch '^Use -system-headers to display errors'
            ) {
                $line
            }
        }

        if ($exit_code -ne 0) {
            throw "$Tool failed for $($Arguments[-1])"
        }
    }

    $include_args = @("-Isrc", "-isystem", $python_include)
    if ($python_plat_include -and $python_plat_include -ne $python_include) {
        $include_args += @("-isystem", $python_plat_include)
    }

    $cFiles = Get-ChildItem src -Recurse -Filter *.c |
        Where-Object {
            if ($IsWindows) {
                $_.Name -ne "posix.c"
            } else {
                $_.Name -ne "windows.c"
            }
        } |
        ForEach-Object { $_.FullName }
    if ($cFiles.Count -eq 0) {
        return
    }

    foreach ($file in $cFiles) {
        $analyzeArgs = @(
            "--analyze",
            "-Xclang",
            "-analyzer-output=text"
        ) + $include_args + @(
            $file
        )
        Invoke-FilteredClangTool "clang" $analyzeArgs

        $tidy_args = @(
            "--quiet",
            "--config-file=$tidy_config",
            "--header-filter=^src[\\\\/].*",
            $file,
            "--"
        ) + $include_args
        Invoke-FilteredClangTool "clang-tidy" $tidy_args

        $check_args = @(
            "-fsyntax-only",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Wpedantic"
        ) + $include_args + @(
            $file
        )
        Invoke-FilteredClangTool "clang" $check_args
    }
