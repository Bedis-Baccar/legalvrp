<#
.SYNOPSIS
  Configure, build and test legalvrp with MSVC + Ninja from any PowerShell.
.EXAMPLE
  ./scripts/build.ps1                     # msvc-debug, build + all tests
  ./scripts/build.ps1 -Preset msvc-release
  ./scripts/build.ps1 -Label unit         # skip Gurobi tests
  ./scripts/build.ps1 -NoTest
#>
param(
  [ValidateSet('msvc-debug', 'msvc-release')]
  [string]$Preset = 'msvc-debug',
  [string]$Label = '',
  [switch]$NoTest,
  [switch]$Fresh
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# Enter the MSVC x64 developer environment once per shell (cl, link, ninja, Windows SDK).
if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
  $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
  $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
  if (-not $vs) { throw 'MSVC (VC.Tools.x86.x64) not found. Install Visual Studio Build Tools with "Desktop development with C++".' }
  Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
  Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
}

# Gurobi: installation, runtime DLL on PATH, licence file.
if (-not $env:GUROBI_HOME) { throw 'GUROBI_HOME is not set (expected e.g. C:\gurobi1203\win64).' }
if ($env:PATH -notlike "*$env:GUROBI_HOME\bin*") { $env:PATH = "$env:GUROBI_HOME\bin;$env:PATH" }
if (-not $env:GRB_LICENSE_FILE) { $env:GRB_LICENSE_FILE = Join-Path $env:USERPROFILE 'gurobi.lic' }
if (-not (Test-Path $env:GRB_LICENSE_FILE)) { throw "Gurobi licence not found: $env:GRB_LICENSE_FILE" }
Write-Host "Gurobi: $env:GUROBI_HOME  licence: $env:GRB_LICENSE_FILE"

# Native tools write warnings to stderr; PowerShell 5.1 must not treat them as errors.
$ErrorActionPreference = 'Continue'

$configureArgs = @('--preset', $Preset)
if ($Fresh) { $configureArgs += '--fresh' }
cmake @configureArgs;              if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build --preset $Preset;    if ($LASTEXITCODE) { exit $LASTEXITCODE }

if (-not $NoTest) {
  $testArgs = @('--preset', $Preset)
  if ($Label) { $testArgs += @('-L', $Label) }
  ctest @testArgs
  exit $LASTEXITCODE
}
