# Собирает готовую к скачиванию папку dist\ASTRIX (и архив dist\ASTRIX.zip) — без личных данных.
#   powershell -ExecutionPolicy Bypass -File tools\make-release.ps1               — только программа (~120 МБ)
#   powershell -ExecutionPolicy Bypass -File tools\make-release.ps1 -WithModels   — вместе с голосами и распознаванием речи (~1 ГБ)
#   -Installer — ещё и установщик dist\ASTRIX-Setup.exe (нужен Inno Setup)
param([switch]$WithModels, [switch]$NoBuild, [switch]$Installer)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$project = Join-Path $root 'ASTRIX'
$release = Join-Path $project 'x64\Release'
$dist = Join-Path $root 'dist\ASTRIX'

if (-not $NoBuild) {
    $msbuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -prerelease -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\amd64\MSBuild.exe' | Select-Object -First 1
    if (-not $msbuild) { throw 'Не нашла MSBuild — установи Visual Studio с «Разработкой классических приложений на C++»' }
    # запущенный ASTRIX.exe не перезаписать — закрываем его на время сборки и потом запускаем снова
    $exe = Join-Path $release 'ASTRIX.exe'
    $running = Get-Process ASTRIX -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe }
    if ($running) {
        Write-Host 'Закрываю запущенную Аню на время сборки…'
        $running | Stop-Process -Force
        $running | Wait-Process -Timeout 10 -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 1
    }
    & $msbuild (Join-Path $project 'ASTRIX.vcxproj') /p:Configuration=Release /p:Platform=x64 /m /v:m /nologo
    $built = $LASTEXITCODE -eq 0
    if ($running) { Start-Process $exe -WorkingDirectory $release }
    if (-not $built) { throw 'Сборка не удалась' }
}

if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory -Force $dist | Out-Null

# сама программа и её библиотеки — личное (anya.ini, scenarios.json, журналы) НЕ копируем
Copy-Item (Join-Path $release 'ASTRIX.exe') $dist
Get-ChildItem $release -Filter 'onnxruntime*.dll' | Copy-Item -Destination $dist
Copy-Item (Join-Path $project 'third_party\llama.cpp') (Join-Path $dist 'llama.cpp') -Recurse
New-Item -ItemType Directory -Force (Join-Path $dist 'third_party\silero') | Out-Null
Copy-Item (Join-Path $project 'third_party\silero\silero_server.py') (Join-Path $dist 'third_party\silero')
Copy-Item (Join-Path $root 'README.md') $dist

if ($WithModels) {
    $models = Join-Path $root 'models'
    New-Item -ItemType Directory -Force (Join-Path $dist 'models') | Out-Null
    foreach ($item in 'ggml-small.bin', 'silero', 'supertonic-3') {
        $path = Join-Path $models $item
        if (Test-Path $path) { Copy-Item $path (Join-Path $dist 'models') -Recurse }
    }
}

# на всякий случай: в папке не должно быть личного
$leaks = Get-ChildItem $dist -Recurse -Include 'anya.ini', 'scenarios.json', 'pult.json', '*.log', 'devprobe.txt'
if ($leaks) { $leaks | Remove-Item -Force; Write-Host "убрала личные файлы: $($leaks.Name -join ', ')" }

$zip = Join-Path $root 'dist\ASTRIX.zip'
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $dist -DestinationPath $zip
Write-Host "Готово: $zip ($([math]::Round((Get-Item $zip).Length / 1MB)) МБ)"

if ($Installer) {
    $iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $iscc) { throw 'Нужен Inno Setup: winget install JRSoftware.InnoSetup' }
    & $iscc /Q (Join-Path $PSScriptRoot 'installer.iss')
    if ($LASTEXITCODE -ne 0) { throw 'Установщик не собрался' }
    $setup = Join-Path $root 'dist\ASTRIX-Setup.exe'
    Write-Host "Готово: $setup ($([math]::Round((Get-Item $setup).Length / 1MB)) МБ)"
}
