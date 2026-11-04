param([Parameter(Mandatory)][string]$BuildRoot)
$ErrorActionPreference = 'Stop'
$source = Join-Path $PSScriptRoot 'wgc_probe.cpp'
New-Item -ItemType Directory -Path $BuildRoot -Force | Out-Null
$env:TEMP=$BuildRoot
$env:TMP=$BuildRoot
$env:PATH="$env:SystemRoot\System32;$env:SystemRoot;$env:ProgramFiles\PowerShell\7"
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -DevCmdArguments '-arch=x64 -host_arch=x64' -SkipAutomaticLocation
Push-Location $BuildRoot
try {
    & cl.exe /nologo /EHsc /O2 /std:c++20 /permissive- $source /Fo:wgc_probe.obj /Fe:wgc_probe.exe /link windowsapp.lib d3d11.lib dxgi.lib user32.lib
    if($LASTEXITCODE) { throw "WGC probe build failed: $LASTEXITCODE" }
} finally { Pop-Location }
