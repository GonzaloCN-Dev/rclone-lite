$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$payload = Join-Path $PSScriptRoot "payload"

$exe = Join-Path $root "build\RcloneLite.exe"
$dll = Join-Path $root "build\slint_cpp.dll"
$crt = Get-ChildItem "$env:ProgramFiles\Microsoft Visual Studio\*\*\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT" -Directory |
    Where-Object { $_.FullName -notmatch "debug" } | Sort-Object FullName | Select-Object -Last 1
foreach ($f in @($exe, $dll)) { if (-not $f -or -not (Test-Path $f)) { throw "Falta: $f" } }

Remove-Item $payload -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory $payload | Out-Null
Copy-Item $exe, $dll, (Join-Path $root "README.md"), (Join-Path $PSScriptRoot "LICENSES.txt") $payload
Get-ChildItem $crt.FullName -Filter *.dll | Where-Object { $_.Name -ne "vccorlib140.dll" -and $_.Name -ne "concrt140.dll" } | Copy-Item -Destination $payload

$iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe") |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw "Inno Setup 6 no está instalado (winget install JRSoftware.InnoSetup)" }

& $iscc (Join-Path $PSScriptRoot "RcloneLite.iss")
if ($LASTEXITCODE -ne 0) { throw "ISCC falló" }
Get-ChildItem (Join-Path $root "dist") -Filter "RcloneLite-Setup-*.exe" | Select-Object Name, @{n="MB";e={[math]::Round($_.Length/1MB,1)}}
