param(
    [string]$PythonInclude = "",
    [string]$Output = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path $PSScriptRoot -Parent

if ([string]::IsNullOrWhiteSpace($Output)) {
    $Output = Join-Path $repoRoot "src\core.zig"
} elseif (-not [System.IO.Path]::IsPathRooted($Output)) {
    $Output = Join-Path $repoRoot $Output
}

if ([string]::IsNullOrWhiteSpace($PythonInclude)) {
    $venvPython = Join-Path $repoRoot ".venv\Scripts\python.exe"
    if (-not (Test-Path $venvPython)) {
        throw "Python include path was not provided and $venvPython was not found."
    }

    $PythonInclude = & $venvPython -c "import sysconfig; print(sysconfig.get_paths()['include'])"
}

$source = Join-Path $repoRoot "src\translate_all.c"
$srcInclude = Join-Path $repoRoot "src"

$translated = & zig translate-c -lc -I $srcInclude -I $PythonInclude $source
if ($LASTEXITCODE -ne 0) {
    throw "zig translate-c failed with exit code $LASTEXITCODE."
}

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($Output, $translated, $utf8NoBom)
