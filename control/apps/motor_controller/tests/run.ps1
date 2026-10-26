param(
    [Parameter(Mandatory = $true)]
    [string]$ZigPath
)

$ErrorActionPreference = 'Stop'
$motorDirectory = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$controlDirectory = (Resolve-Path -LiteralPath (Join-Path $motorDirectory '../..')).Path
$testBuildDirectory = Join-Path $controlDirectory 'build/motor-controller-host-tests'
New-Item -ItemType Directory -Path $testBuildDirectory -Force | Out-Null
$testExecutable = Join-Path $testBuildDirectory 'test_motor_controller.exe'

& $ZigPath cc -std=c11 -O1 -Wall -Wextra -Werror `
    "-I$motorDirectory" `
    (Join-Path $PSScriptRoot 'test_motor_controller.c') `
    (Join-Path $motorDirectory 'motor_controller.c') `
    (Join-Path $motorDirectory 'foc/foc.c') `
    -o $testExecutable
if ($LASTEXITCODE -ne 0) { throw "Host test compilation failed ($LASTEXITCODE)." }

& $testExecutable
if ($LASTEXITCODE -ne 0) { throw "Motor controller regression failed ($LASTEXITCODE)." }
