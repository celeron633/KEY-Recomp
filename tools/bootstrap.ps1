param([switch]$WithClang)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$cache = Join-Path $root '.tools/downloads'
New-Item -ItemType Directory -Force $cache | Out-Null
$lock = Get-Content (Join-Path $root 'toolchain.lock.json') -Raw | ConvertFrom-Json
foreach ($tool in $lock.tools) {
    if ($tool.name -eq 'llvm-mingw' -and -not $WithClang) { continue }
    $archive = Join-Path $cache $tool.archive
    if (-not (Test-Path -LiteralPath $archive)) {
        Invoke-WebRequest $tool.url -OutFile $archive
    }
    if ((Get-FileHash -LiteralPath $archive -Algorithm $tool.algorithm).Hash -ne $tool.hash) {
        throw "Checksum mismatch: $($tool.name). Preserve/remove the incomplete archive manually before retrying."
    }
    $executable = Join-Path $root $tool.executable
    if (-not (Test-Path -LiteralPath $executable)) {
        Expand-Archive -LiteralPath $archive -DestinationPath (Join-Path $root $tool.destination)
    }
    & $executable --version
    if ($LASTEXITCODE -ne 0) { throw "Tool validation failed: $($tool.name)" }
}
