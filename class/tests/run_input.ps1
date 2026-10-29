$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../..").Path
$compiler = 'D:/0_software/msys64/mingw64/bin/gcc.exe'
if (-not (Test-Path $compiler)) {
    $compiler = (Get-Command gcc -ErrorAction Stop).Source
}
New-Item -ItemType Directory -Force "$project/build" | Out-Null
$output = "$project/build/test_input_driver.exe"
$savedPath = $env:Path
$env:Path = "$(Split-Path $compiler);$savedPath"
try {
    & $compiler -std=gnu11 -Wall -Wextra -Werror -DTEST_INPUT `
        "-I$PSScriptRoot/fake" "-I$project/class" "-I$project/modus/src" `
        "$project/class/input_driver.c" "$PSScriptRoot/input_driver_test.c" -o $output
    if ($LASTEXITCODE -ne 0) { throw 'input compilation failed' }
    & $output
    if ($LASTEXITCODE -ne 0) { throw 'input test failed' }
} finally { $env:Path = $savedPath }
