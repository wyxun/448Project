param([ValidateSet('all','driver','mvc','pwm')][string]$Module = 'all')
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../..").Path
$compiler = 'D:/0_software/msys64/mingw64/bin/gcc.exe'
if (-not (Test-Path $compiler)) {
    $compiler = (Get-Command gcc -ErrorAction Stop).Source
}
New-Item -ItemType Directory -Force "$project/build" | Out-Null
$output = "$project/build/test_rf448.exe"
$savedPath = $env:Path
$env:Path = "$(Split-Path $compiler);$savedPath"
try {
if ($Module -ne 'pwm') {
& $compiler -std=gnu11 -Wall -Wextra -Werror -Wno-implicit-fallthrough `
    "-I$PSScriptRoot/fake" "-I$project/class" "-I$project/modus/src" `
    "-I$project/modus/src/mdebug" -DMSHELL_ENABLE=1 `
    "$project/class/rf448_driver.c" "$project/class/mvc.c" `
    "$project/modus/src/mdebug/mshell_parse.c" `
    "$PSScriptRoot/rf448_test.c" -o $output
if ($LASTEXITCODE -ne 0) { throw 'RF448 compilation failed' }
& $output $Module
if ($LASTEXITCODE -ne 0) { throw 'RF448 test failed' }
}
if ($Module -eq 'all' -or $Module -eq 'pwm') {
    $pwmOutput = "$project/build/test_rf448_pwm.exe"
    & $compiler -std=gnu11 -Wall -Wextra -Werror `
        "-I$PSScriptRoot/fake" "-I$project/modus/src" `
        "$PSScriptRoot/pwm_control_test.c" -o $pwmOutput
    if ($LASTEXITCODE -ne 0) { throw 'PWM check compilation failed' }
    & $pwmOutput
    if ($LASTEXITCODE -ne 0) { throw 'PWM check failed' }
}
} finally { $env:Path = $savedPath }
