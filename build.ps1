param(
    [int]$Jobs = 3,
    [string]$ToolchainBin = $env:MIPS_SDE_BIN,
    [string]$MakePath = $env:MAKE_EXE
)
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ToolchainBin)) {
    $compiler = Get-Command 'mips-sde-elf-gcc.exe' -ErrorAction SilentlyContinue
    if ($compiler) { $ToolchainBin = Split-Path -Parent $compiler.Source }
}
if ([string]::IsNullOrWhiteSpace($ToolchainBin)) {
    throw 'Set MIPS_SDE_BIN to the SDE Lite bin directory, or add it to PATH.'
}

$cc = Join-Path $ToolchainBin 'mips-sde-elf-gcc.exe'
$objcopy = Join-Path $ToolchainBin 'mips-sde-elf-objcopy.exe'
$size = Join-Path $ToolchainBin 'mips-sde-elf-size.exe'
foreach ($tool in @($cc, $objcopy, $size)) {
    if (!(Test-Path -LiteralPath $tool -PathType Leaf)) { throw "Missing tool: $tool" }
}

if ([string]::IsNullOrWhiteSpace($MakePath)) {
    $makeCommand = Get-Command 'make.exe' -ErrorAction SilentlyContinue
    if ($makeCommand) { $MakePath = $makeCommand.Source }
}
if ([string]::IsNullOrWhiteSpace($MakePath) -or !(Test-Path -LiteralPath $MakePath -PathType Leaf)) {
    throw 'Set MAKE_EXE to make.exe, or add make.exe to PATH.'
}

Push-Location (Join-Path $PSScriptRoot 'Build')
try {
    # Windows PowerShell treats native stderr (including warnings) as errors.
    $ErrorActionPreference = 'Continue'
    & $MakePath -f Makefile.mk "-j$Jobs" "CC=$($cc.Replace('\', '/'))" `
        "OBJCOPY=$($objcopy.Replace('\', '/'))" "SIZE=$($size.Replace('\', '/'))" all
    $buildExit = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($buildExit -ne 0) { throw "MIPS build failed: exit $buildExit" }
} finally { $ErrorActionPreference = 'Stop'; Pop-Location }
