# Pack the Windows Release layout described in INSTALL.md.
# The Inno Setup scripts are not in this repository, so CI publishes this portable layout.
# Actions uploads the staged directory; uploading the zip as well would wrap it in a second zip.
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$cmake = Get-Content -LiteralPath (Join-Path $Root "CMakeLists.txt") -Raw
if ($cmake -notmatch 'project\(OpenTune VERSION (\d+\.\d+\.\d+)') {
    throw "Could not parse the version from CMakeLists.txt"
}
$Version = $Matches[1]
$Label = ""
if ($env:OPENTUNE_BUILD_LABEL) {
    $Label = $env:OPENTUNE_BUILD_LABEL.Trim()
}
if ($Label -and $Label -notmatch '^[0-9A-Za-z._-]{1,40}$') {
    throw "Invalid OPENTUNE_BUILD_LABEL: $Label"
}
$Stem = $Version
if ($Label) {
    $Stem = "$Version-$Label"
}
if ($cmake -notmatch 'set\(OPENTUNE_ORT_DLL_NAME "([^"]+)"\)') {
    throw "Could not parse OPENTUNE_ORT_DLL_NAME from CMakeLists.txt"
}
$OrtDll = $Matches[1]

$Standalone = Join-Path $Root "build-ara-overlay-vs18-clean\OpenTuneStandalone_artefacts\Release\Standalone"
$Vst3 = Join-Path $Root "build-ara-overlay-vs18-clean\OpenTune_artefacts\Release\VST3\OpenTune.vst3"

function Assert-File {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Missing build output: $Path"
    }
}

Assert-File (Join-Path $Standalone "OpenTune.exe")
Assert-File (Join-Path $Standalone $OrtDll)
Assert-File (Join-Path $Standalone "DirectML.dll")
Assert-File (Join-Path $Standalone "D3D12\D3D12Core.dll")
Assert-File (Join-Path $Standalone "D3D12\D3D12SDKLayers.dll")
Assert-File (Join-Path $Standalone "models\fcpe.onnx")
Assert-File (Join-Path $Standalone "models\hifigan.onnx")
Assert-File $Vst3
$vstOrt = Get-ChildItem -LiteralPath $Vst3 -Recurse -Filter $OrtDll | Select-Object -First 1
if (-not $vstOrt) {
    throw "VST3 is missing $OrtDll"
}

$exeVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $Standalone "OpenTune.exe")).ProductVersion
if ($exeVersion -notlike "$Version*") {
    throw "OpenTune.exe product version '$exeVersion' does not match CMakeLists.txt version $Version"
}

$Stage = Join-Path $Root "dist\windows-stage"
$Dist = Join-Path $Root "dist"
if (Test-Path -LiteralPath $Stage) {
    Remove-Item -LiteralPath $Stage -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $Stage | Out-Null
Copy-Item -Path (Join-Path $Standalone "*") -Destination $Stage -Recurse
Copy-Item -LiteralPath $Vst3 -Destination (Join-Path $Stage "OpenTune.vst3") -Recurse
foreach ($name in @("LICENSE", "NOTICE.txt", "NOTICE.zh-CN.txt", "STATEMENTS.txt")) {
    $source = Join-Path $Root $name
    if (Test-Path -LiteralPath $source) {
        Copy-Item -LiteralPath $source -Destination $Stage
    }
}

$zip = Join-Path $Dist "OpenTune-$Stem-Windows-x64.zip"
if (Test-Path -LiteralPath $zip) {
    Remove-Item -LiteralPath $zip -Force
}
New-Item -ItemType Directory -Force -Path $Dist | Out-Null
# Passing "." makes bsdtar store every name as "./file", which archives show as ".\".
$names = @(Get-ChildItem -LiteralPath $Stage -Force | ForEach-Object { $_.Name })
if ($names.Count -eq 0) {
    throw "Windows staging directory is empty"
}
& tar.exe -a -c -f $zip -C $Stage -- @names
if ($LASTEXITCODE -ne 0) {
    throw "Failed to create the zip package"
}
$prefixed = & tar.exe -tf $zip | Where-Object { $_ -like "./*" -or $_ -like ".\*" }
if ($prefixed) {
    throw "Zip entries must not start with ./ : $($prefixed -join ', ')"
}
if ($env:GITHUB_OUTPUT) {
    [System.IO.File]::AppendAllText($env:GITHUB_OUTPUT, "version=$Stem`n")
}
Write-Host "Created $zip"
