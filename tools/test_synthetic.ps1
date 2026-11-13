$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    python tools/make_synthetic.py
    if ($LASTEXITCODE -ne 0) { throw 'Synthetic input generation failed' }
    & .tools/dotnet/dotnet.exe references/WiiCompiled/translator/src/Translator.Cli/bin/Release/net8.0/Translator.Cli.dll translate-recursive 0x80004000 --project private/synthetic/recomp.yml --threads 1 *> private/reports/synthetic-translation.log
    if ($LASTEXITCODE -ne 0) { throw 'Synthetic translation failed' }
    & .tools/llvm-mingw-20261104-ucrt-x86_64/bin/clang++.exe -std=c++20 -O2 -static -I references/WiiCompiled/runtime/include private/synthetic/generated/functions/func_80004000.cpp tests/native_add.cpp -o private/synthetic/native-add.exe
    if ($LASTEXITCODE -ne 0) { throw 'Synthetic native compilation failed' }
    & private/synthetic/native-add.exe | Tee-Object private/reports/synthetic-native.log
    if ($LASTEXITCODE -ne 0) { throw 'Synthetic native execution failed' }
} finally {
    Pop-Location
}
