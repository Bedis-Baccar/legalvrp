<#
.SYNOPSIS
  Build (if needed) and open the visual application of legalvrp (docs/APP.md).
.EXAMPLE
  ./scripts/demo.ps1               # build what is missing, start the server, open the browser
  ./scripts/demo.ps1 -Rebuild      # recompute every scenario (instances, runs, bundles)
  ./scripts/demo.ps1 -Port 9000 -NoBrowser
.NOTES
  Needs the C++ toolchain of scripts/build.ps1 (MSVC, Ninja, Gurobi), Python >= 3.11 and, the
  first time only, Node.js >= 20 to build the web application (app/dist).
#>
param(
  [switch]$Rebuild,
  [switch]$NoBrowser,
  [int]$Port = 8765
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

function Find-Python {
  foreach ($c in @(@{ Exe = 'python'; Args = @() }, @{ Exe = 'py'; Args = @('-3') })) {
    if (Get-Command $c.Exe -ErrorAction SilentlyContinue) {
      $pa = $c.Args
      $ok = & $c.Exe @pa -c "import sys; print(sys.version_info >= (3, 11))" 2>$null
      if ($ok -eq 'True') { return $c }
    }
  }
  throw 'Python >= 3.11 not found (python or py on PATH).'
}
$py = Find-Python
$pyExe = $py.Exe
$pyArgs = $py.Args

# 1. C++ tools (release build with Gurobi, as for the experiments).
$bin = Join-Path $root 'build\msvc-release\apps'
if (-not (Test-Path (Join-Path $bin 'legalvrp-export.exe'))) {
  Write-Host '== building the C++ tools (msvc-release)'
  & (Join-Path $PSScriptRoot 'build.ps1') -Preset msvc-release -NoTest
}
if ($env:GUROBI_HOME -and ($env:PATH -notlike "*$env:GUROBI_HOME\bin*")) { $env:PATH = "$env:GUROBI_HOME\bin;$env:PATH" }

# 2. Scenarios (instances, plans, bundles) -> results/demo.
if ($Rebuild -or -not (Test-Path (Join-Path $root 'results\demo\index.json'))) {
  Write-Host '== building the scenarios'
  $force = if ($Rebuild) { @('--force') } else { @() }
  & $pyExe @pyArgs (Join-Path $PSScriptRoot 'build_demo.py') --bin $bin @force
  if ($LASTEXITCODE -ne 0) { throw 'build_demo.py failed' }
}

# 3. Web application -> app/dist (Node.js only needed here).
if (-not (Test-Path (Join-Path $root 'app\dist\index.html'))) {
  if (-not (Get-Command npm -ErrorAction SilentlyContinue)) { throw 'app/dist is missing and npm is not installed (Node.js >= 20).' }
  Write-Host '== building the web application'
  Push-Location (Join-Path $root 'app')
  try {
    $ErrorActionPreference = 'Continue'
    npm install --no-fund --no-audit
    npm run build
    if ($LASTEXITCODE -ne 0) { throw 'npm run build failed' }
  } finally { Pop-Location; $ErrorActionPreference = 'Stop' }
}

# 4. Serve.
$open = if ($NoBrowser) { @() } else { @('--open') }
& $pyExe @pyArgs (Join-Path $PSScriptRoot 'demo_server.py') --port $Port @open
