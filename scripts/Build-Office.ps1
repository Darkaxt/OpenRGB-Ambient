param([Parameter(Mandatory)][string]$QtRoot,[Parameter(Mandatory)][string]$OpenRGBSource,[Parameter(Mandatory)][string]$BuildRoot)
$ErrorActionPreference='Stop'
$source=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$git=(Get-Command git -ErrorAction Stop).Source
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs) { throw 'MSVC not found' }
$env:PATH="$env:SystemRoot\System32;$env:SystemRoot;$PSHOME"
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -DevCmdArguments '-arch=x64 -host_arch=x64' -SkipAutomaticLocation
$env:PATH=(Join-Path $QtRoot 'bin')+';'+$env:PATH
New-Item -ItemType Directory -Path $BuildRoot -Force | Out-Null
$cmake=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
& $cmake -S $source -B $BuildRoot -G 'Visual Studio 17 2022' -A x64 -DQT_VERSION=6 "-DCMAKE_PREFIX_PATH=$QtRoot" "-DOPENRGB_SOURCE_DIR=$OpenRGBSource" -DBUILD_TESTING=ON
if($LASTEXITCODE) { throw 'CMake configure failed' }
& $cmake --build $BuildRoot --config Release --parallel 2
if($LASTEXITCODE) { throw 'Plugin build failed' }
$ctest=Join-Path (Split-Path $cmake) 'ctest.exe'
& $ctest --test-dir $BuildRoot -C Release --output-on-failure
if($LASTEXITCODE) { throw 'GPU regression check failed' }
Copy-Item (Join-Path $QtRoot 'bin/Qt6*.dll') (Join-Path $BuildRoot 'Release')
@{source=(& $git -C $source rev-parse HEAD);openrgb=(& $git -C $OpenRGBSource rev-parse HEAD);qt='6.8.3'} | ConvertTo-Json | Set-Content (Join-Path $BuildRoot 'Release/office-plugin-build.json')
