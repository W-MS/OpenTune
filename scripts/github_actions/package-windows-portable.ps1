# Pack the Windows Release layout described in INSTALL.md.
# The Inno Setup scripts are not in this repository, so the CI artifact is the portable zip.
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$cmake = Get-Content -LiteralPath (Join-Path $Root "CMakeLists.txt") -Raw
if ($cmake -notmatch 'project\(OpenTune VERSION (\d+\.\d+\.\d+)') {
    throw "无法从 CMakeLists.txt 解析版本号"
}
$Version = $Matches[1]
if ($cmake -notmatch 'set\(OPENTUNE_ORT_DLL_NAME "([^"]+)"\)') {
    throw "无法从 CMakeLists.txt 解析 OPENTUNE_ORT_DLL_NAME"
}
$OrtDll = $Matches[1]

$Standalone = Join-Path $Root "build-ara-overlay-vs18-clean\OpenTuneStandalone_artefacts\Release\Standalone"
$Vst3 = Join-Path $Root "build-ara-overlay-vs18-clean\OpenTune_artefacts\Release\VST3\OpenTune.vst3"

function Assert-File {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "缺少产物: $Path"
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
    throw "VST3 中缺少 $OrtDll"
}

$exeVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $Standalone "OpenTune.exe")).ProductVersion
if ($exeVersion -notlike "$Version*") {
    throw "OpenTune.exe 产品版本 '$exeVersion' 与 CMakeLists.txt 的 $Version 不一致"
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

$zip = Join-Path $Dist "OpenTune-$Version-Windows-x64.zip"
if (Test-Path -LiteralPath $zip) {
    Remove-Item -LiteralPath $zip -Force
}
New-Item -ItemType Directory -Force -Path $Dist | Out-Null
& tar.exe -a -c -f $zip -C $Stage .
if ($LASTEXITCODE -ne 0) {
    throw "打包 zip 失败"
}
Write-Host "已生成 $zip"
