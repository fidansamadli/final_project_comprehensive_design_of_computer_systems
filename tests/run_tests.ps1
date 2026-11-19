# Host test driver for the LED Morse Code Messenger.
# Compiles main.c (with MORSE_HOST_TEST) into the test program and runs it.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$gcc = 'C:\mingw\bin\gcc.exe'
if (!(Test-Path -LiteralPath $gcc)) { $gcc = 'gcc' }
$exe = Join-Path $PSScriptRoot 'test_morse.exe'

Push-Location $root
try {
    & $gcc -std=c99 -Wall -Wextra -Werror -I. -o $exe 'tests/test_morse.c'
    if ($LASTEXITCODE -ne 0) { throw "host build failed: $LASTEXITCODE" }
    & $exe | Tee-Object -FilePath (Join-Path $PSScriptRoot 'test-results.txt')
    $code = $LASTEXITCODE
} finally { Pop-Location }

if ($code -ne 0) { throw "host tests failed: $code" }
Write-Output "HOST TESTS PASS"
