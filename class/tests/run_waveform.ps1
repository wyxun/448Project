$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../..").Path
$compiler = 'D:/0_software/msys64/mingw64/bin/gcc.exe'
if (-not (Test-Path $compiler)) {
    $compiler = (Get-Command gcc -ErrorAction Stop).Source
}
$sources = @(
    "$project/class/mircoelec.c",
    "$project/class/dac_driver.c",
    "$project/class/mircoelec_waveform.c",
    "$project/modus/src/mdebug/mshell_parse.c",
    "$project/modus/src/mdebug/mwaveform.c",
    "$project/modus/src/mdebug/mwaveform_protocol.c",
    "$PSScriptRoot/test_support.c",
    "$PSScriptRoot/mircoelec_waveform_test.c"
)
$defines = @('-DMWAVEFORM_ENABLE=1', '-DMWAVEFORM_MAX_CHANNELS=10',
    '-DMWAVEFORM_RTT_BUFFER_SIZE=4096')
New-Item -ItemType Directory -Force "$project/build" | Out-Null
$output = "$project/build/test_mircoelec_waveform.exe"
$savedPath = $env:Path
$env:Path = "$(Split-Path $compiler);$savedPath"
try {
    & $compiler -std=gnu11 -Wall -Wextra -Werror `
        "-I$PSScriptRoot/fake" "-I$project/class" "-I$project/modus/src" `
        $defines $sources -lm -o $output
    if ($LASTEXITCODE -ne 0) { throw 'Waveform compilation failed' }
    & $output
    if ($LASTEXITCODE -ne 0) { throw 'Waveform test failed' }
} finally { $env:Path = $savedPath }
