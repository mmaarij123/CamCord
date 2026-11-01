$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$runId = [guid]::NewGuid().ToString('N')
$artifacts = Join-Path $projectRoot "obj\installer-tests\run-$runId"
$fixture = Join-Path $artifacts 'installed'
$compilerOutput = Join-Path $artifacts 'setup'
New-Item -ItemType Directory -Path $fixture,$compilerOutput -Force | Out-Null
$compiler = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") |
    Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $compiler) { throw 'Inno Setup 6 is required.' }
$startupTestKey = "Software\CamCord\Tests\Installer-$runId"
& $compiler /Q /DInstallerSmokeTest "/DStartupTestKey=$startupTestKey" "/O$compilerOutput" (Join-Path $projectRoot 'installer\CamCord.iss')
if ($LASTEXITCODE -ne 0) { throw 'Isolated installer compilation failed.' }
# Reuse only the already verified test engine; no upstream download is needed.
Copy-Item -LiteralPath (Join-Path $projectRoot 'bin\Release\ffmpeg.exe') -Destination $fixture
$setup = Join-Path $compilerOutput 'CamCord-Setup.exe'
$arguments = @('/SP-','/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',('/DIR="' + $fixture + '"'))
foreach ($stage in @('install','upgrade')) {
    if ($stage -eq 'upgrade') {
        Set-Content -LiteralPath (Join-Path $fixture 'ui\assets\obsolete-test.js') -Value 'retired UI bundle'
        Set-Content -LiteralPath (Join-Path $fixture 'licenses\obsolete-test.txt') -Value 'retired notice'
        Set-Content -LiteralPath (Join-Path $fixture 'settings.ini') -Value 'owned test preferences'
        Set-Content -LiteralPath (Join-Path $fixture 'recording.mp4') -Value 'owned test recording marker'
    }
    $installation = Start-Process -FilePath $setup -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
    if ($installation.ExitCode -ne 0) { throw "Isolated $stage failed: $($installation.ExitCode)" }
}
foreach ($relative in @('ui\assets\obsolete-test.js','licenses\obsolete-test.txt')) {
    if (Test-Path -LiteralPath (Join-Path $fixture $relative)) { throw "Upgrade retained $relative" }
}
if ((Get-Content -LiteralPath (Join-Path $fixture 'settings.ini') -Raw).Trim() -ne 'owned test preferences' -or
    (Get-Content -LiteralPath (Join-Path $fixture 'recording.mp4') -Raw).Trim() -ne 'owned test recording marker') {
    throw 'Upgrade changed files outside generated UI/license folders.'
}
$version = (Get-Content -LiteralPath (Join-Path $projectRoot 'ui\package.json') -Raw | ConvertFrom-Json).version
if ((Get-Item -LiteralPath (Join-Path $fixture 'CamCord.exe')).VersionInfo.ProductVersion.Trim() -ne $version) { throw 'Installed version mismatch.' }
$stagedUi = Join-Path $projectRoot 'bin\Release\ui'
foreach ($file in Get-ChildItem -LiteralPath $stagedUi -File -Recurse) {
    $relative = $file.FullName.Substring($stagedUi.Length + 1)
    if ((Get-FileHash -LiteralPath $file.FullName).Hash -ne (Get-FileHash -LiteralPath (Join-Path $fixture "ui\$relative")).Hash) {
        throw "Installed UI mismatch: $relative"
    }
}
Write-Host 'PASS: isolated install/upgrade, exact version/UI, obsolete asset cleanup and preference/recording preservation'
$resolvedFixture = (Resolve-Path -LiteralPath $fixture).Path
if ($resolvedFixture -ne [IO.Path]::GetFullPath((Join-Path $artifacts 'installed'))) { throw 'Unexpected isolated uninstall target.' }
$uninstall = Start-Process -FilePath (Join-Path $fixture 'unins000.exe') -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART' -WindowStyle Hidden -Wait -PassThru
if ($uninstall.ExitCode -ne 0 -or (Test-Path -LiteralPath (Join-Path $fixture 'CamCord.exe'))) { throw 'Isolated uninstall failed.' }
Write-Host "PASS: isolated uninstall; artifacts: $artifacts. The real installation and settings were not changed."
