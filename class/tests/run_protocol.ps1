$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../..").Path
$compiler = 'D:/0_software/msys64/mingw64/bin/gcc.exe'
if (-not (Test-Path $compiler)) {
    $compiler = (Get-Command gcc -ErrorAction Stop).Source
}
New-Item -ItemType Directory -Force "$project/build" | Out-Null
$output = "$project/build/test_board_protocol.exe"
$savedPath = $env:Path
$env:Path = "$(Split-Path $compiler);$savedPath"
try {
& $compiler -std=c11 -Wall -Wextra -Werror "-I$project/class" "-I$project/modus/src" `
    "$project/class/board_protocol.c" `
    "$project/modus/src/utilities/mringbuf.c" `
    "$PSScriptRoot/board_protocol_test.c" -o $output
if ($LASTEXITCODE -ne 0) { throw 'Protocol compilation failed' }
    & $output
    if ($LASTEXITCODE -ne 0) { throw 'Protocol test failed' }
} finally { $env:Path = $savedPath }

