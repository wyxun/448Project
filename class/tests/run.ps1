param(
    [ValidateSet('all', 'dac', 'dwin', 'mircoelec', 'mvc', 'ui', 'link', 'adc_service')]
    [string]$Module = 'all',
    [ValidateSet(0, 1)][int]$Crc = 1,
    [ValidateSet(0, 1)][int]$SerialShell = 0,
    [ValidateSet('master', 'client')][string]$AdcBoard = 'master'
)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../..").Path
$compiler = 'D:/0_software/msys64/mingw64/bin/gcc.exe'
if (-not (Test-Path $compiler)) {
    $compiler = (Get-Command gcc -ErrorAction Stop).Source
}
$dependencies = @{
    dac = @('dac_driver')
    dwin = @('dwin_driver')
    mircoelec = @('mircoelec', 'dac_driver', 'mircoelec_waveform')
    mvc = @('mvc', 'mvc_ui', 'dwin_driver', 'board_link_driver')
    ui = @('mvc_ui', 'dwin_driver')
    link = @('board_link_driver')
    adc_service = @('../peripheral/at32f415/mdi/service')
}
New-Item -ItemType Directory -Force "$project/build" | Out-Null
$modules = if ($Module -eq 'all') {
    @('dac', 'dwin', 'mircoelec', 'mvc', 'ui', 'link', 'adc_service')
} else { @($Module) }
$savedPath = $env:Path
$env:Path = "$(Split-Path $compiler);$savedPath"
try {
    foreach ($name in $modules) {
        $sources = $dependencies[$name] |
            ForEach-Object { "$project/class/$_.c" }
        if ($name -eq 'mircoelec' -or $name -eq 'mvc') {
            $sources += "$project/modus/src/mdebug/mshell_parse.c"
        }
        $testAdcTickHz = '144000000UL'
        if ($name -eq 'adc_service' -and $AdcBoard -eq 'client') {
            $sources = @("$project/../M448_client/peripheral/at32f421/mdi/service.c")
            $testAdcTickHz = '48000000UL'
        }
        $output = "$project/build/test_${name}_crc$Crc.exe"
        & $compiler -std=gnu11 -Wall -Wextra -Werror `
            "-I$PSScriptRoot/fake" "-I$project/class" "-I$project/modus/src" `
            "-DUSERCONFIG_MSHELL_ON_SERIAL=$SerialShell" `
            "-DTEST_ADC_TICK_HZ=$testAdcTickHz" `
            "-DTEST_$($name.ToUpper())" "-DDWIN_DRIVER_CRC16_EN=$Crc" `
            $sources "$PSScriptRoot/test_support.c" `
            "$PSScriptRoot/business_test.c" -o $output
        if ($LASTEXITCODE -ne 0) { throw "$name compilation failed" }
        Write-Host "Testing $name (CRC=$Crc)"
        & $output
        if ($LASTEXITCODE -ne 0) { throw "$name failed" }
    }
} finally { $env:Path = $savedPath }
