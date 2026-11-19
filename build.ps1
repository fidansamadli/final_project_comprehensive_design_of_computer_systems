param([int]$Jobs = 3)
$ErrorActionPreference = 'Stop'
$compilerDir = 'D:\LoongIDE\mips-2015.05\bin'
$make = 'C:\msys\1.0\bin\make.exe'
if (!(Test-Path -LiteralPath $make)) { throw "Missing build tool: $make" }
$env:PATH = "$compilerDir;C:\msys\1.0\bin;$env:PATH"
Push-Location (Join-Path $PSScriptRoot 'Build')
try {
    # Windows PowerShell treats native stderr (including warnings) as errors.
    $ErrorActionPreference = 'Continue'
    & $make -f Makefile.mk "-j$Jobs" all
    $buildExit = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($buildExit -ne 0) { throw "MIPS build failed: exit $buildExit" }
} finally { $ErrorActionPreference = 'Stop'; Pop-Location }
