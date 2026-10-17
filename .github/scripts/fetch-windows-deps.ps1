# Download the Windows dependencies listed in BUILDING.md into this runner workspace.
# Nothing here is written back to the repository.
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
Set-Location $Root

$Work = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { Join-Path $env:TEMP "opentune-ci" }
New-Item -ItemType Directory -Force -Path $Work, (Join-Path $Root "ThirdParty"), (Join-Path $Root "models") | Out-Null

function Invoke-Checked {
    param([string]$File, [string[]]$CommandArguments)
    & $File @CommandArguments
    if ($LASTEXITCODE -ne 0) {
        throw "$File 失败，退出码 $LASTEXITCODE"
    }
}

function Save-Url {
    param([string]$Url, [string]$Destination)
    Invoke-Checked "curl.exe" @("-L", "--fail", "--retry", "5", "--retry-delay", "2", "-o", $Destination, $Url)
}

if (-not (Test-Path -LiteralPath (Join-Path $Root "JUCE-master\CMakeLists.txt"))) {
    if (Test-Path -LiteralPath (Join-Path $Root "JUCE-master")) {
        Remove-Item -LiteralPath (Join-Path $Root "JUCE-master") -Recurse -Force
    }
    Invoke-Checked "git" @(
        "-c", "core.longpaths=true",
        "clone", "--depth", "1",
        "https://github.com/juce-framework/JUCE.git",
        (Join-Path $Root "JUCE-master")
    )
}

$Python = (Get-Command python -ErrorAction SilentlyContinue).Source
if (-not $Python) {
    throw "虚拟机上没有 python，无法应用 JUCE ARA 补丁或下载模型"
}
Invoke-Checked $Python @((Join-Path $Root ".github\scripts\patch-juce-ara.py"))

$AraDir = Join-Path $Root "ThirdParty\ARA_SDK-releases-2.3.0"
if (-not (Test-Path -LiteralPath (Join-Path $AraDir ".git"))) {
    if (Test-Path -LiteralPath $AraDir) {
        Remove-Item -LiteralPath $AraDir -Recurse -Force
    }
    Invoke-Checked "git" @(
        "-c", "core.longpaths=true",
        "clone", "--depth", "1", "--branch", "releases/2.3.0",
        "https://github.com/Celemony/ARA_SDK.git",
        $AraDir
    )
}
Invoke-Checked "git" @("-C", $AraDir, "-c", "core.longpaths=true", "submodule", "update", "--init", "--recursive")

$SoundTouch = Join-Path $Root "ThirdParty\soundtouch-2.3.3\source\SoundTouch\SoundTouch.cpp"
if (-not (Test-Path -LiteralPath $SoundTouch)) {
    $SoundTouchDir = Join-Path $Root "ThirdParty\soundtouch-2.3.3"
    if (Test-Path -LiteralPath $SoundTouchDir) {
        Remove-Item -LiteralPath $SoundTouchDir -Recurse -Force
    }
    Invoke-Checked "git" @(
        "clone", "--depth", "1", "--branch", "2.3.3",
        "https://codeberg.org/soundtouch/soundtouch.git",
        $SoundTouchDir
    )
}

function Expand-Nupkg {
    param([string]$Url, [string]$Destination)
    $zip = Join-Path $Work ((Split-Path $Destination -Leaf) + ".zip")
    Save-Url $Url $zip
    if (Test-Path -LiteralPath $Destination) {
        Remove-Item -LiteralPath $Destination -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Invoke-Checked "tar.exe" @("-xf", $zip, "-C", $Destination)
}

Expand-Nupkg `
    "https://www.nuget.org/api/v2/package/Microsoft.ML.OnnxRuntime.DirectML/1.24.4" `
    (Join-Path $Root "ThirdParty\onnxruntime-dml-1.24.4")
Expand-Nupkg `
    "https://www.nuget.org/api/v2/package/Microsoft.AI.DirectML/1.15.4" `
    (Join-Path $Root "ThirdParty\microsoft.ai.directml.1.15.4")
Expand-Nupkg `
    "https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/1.619.5" `
    (Join-Path $Root "ThirdParty\microsoft.direct3d.d3d12.1.619.5")

$OrtZip = Join-Path $Work "onnxruntime-win-x64-1.24.4.zip"
$OrtExtract = Join-Path $Work "onnxruntime-win-x64-1.24.4"
Save-Url "https://github.com/microsoft/onnxruntime/releases/download/v1.24.4/onnxruntime-win-x64-1.24.4.zip" $OrtZip
if (Test-Path -LiteralPath $OrtExtract) {
    Remove-Item -LiteralPath $OrtExtract -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $OrtExtract | Out-Null
Invoke-Checked "tar.exe" @("-xf", $OrtZip, "-C", $OrtExtract)
$header = Get-ChildItem -Path $OrtExtract -Recurse -Filter "onnxruntime_cxx_api.h" | Select-Object -First 1
if (-not $header) {
    throw "ONNX Runtime CPU 包中没有 include/onnxruntime_cxx_api.h"
}
$packageRoot = $header.Directory.Parent.FullName
$ortTarget = Join-Path $Root "ThirdParty\onnxruntime-win-x64-1.24.4"
if (Test-Path -LiteralPath $ortTarget) {
    Remove-Item -LiteralPath $ortTarget -Recurse -Force
}
Move-Item -LiteralPath $packageRoot -Destination $ortTarget

Invoke-Checked $Python @((Join-Path $Root ".github\scripts\fetch-models.py"))

$required = @(
    "ThirdParty\onnxruntime-win-x64-1.24.4\include\onnxruntime_cxx_api.h",
    "ThirdParty\onnxruntime-dml-1.24.4\build\native\include\dml_provider_factory.h",
    "ThirdParty\onnxruntime-dml-1.24.4\runtimes\win-x64\native\onnxruntime.dll",
    "ThirdParty\microsoft.ai.directml.1.15.4\include\DirectML.h",
    "ThirdParty\microsoft.ai.directml.1.15.4\bin\x64-win\DirectML.dll",
    "ThirdParty\microsoft.ai.directml.1.15.4\bin\x64-win\DirectML.lib",
    "ThirdParty\microsoft.direct3d.d3d12.1.619.5\build\native\include\d3d12.h",
    "ThirdParty\microsoft.direct3d.d3d12.1.619.5\build\native\include\d3dx12",
    "ThirdParty\microsoft.direct3d.d3d12.1.619.5\build\native\bin\x64\D3D12Core.dll",
    "ThirdParty\microsoft.direct3d.d3d12.1.619.5\build\native\bin\x64\d3d12SDKLayers.dll",
    "ThirdParty\r8brain-free-src-master\r8bbase.h",
    "ThirdParty\soundtouch-2.3.3\source\SoundTouch\SoundTouch.cpp",
    "ThirdParty\ARA_SDK-releases-2.3.0\ARA_Library",
    "models\fcpe.onnx",
    "pc_nsf_hifigan_44.1k_ONNX\pc_nsf_hifigan_44.1k_hop512_128bin_2025.02.onnx"
)
foreach ($relative in $required) {
    $path = Join-Path $Root $relative
    if (-not (Test-Path -LiteralPath $path)) {
        throw "依赖未放到 BUILDING.md 要求的路径: $relative"
    }
}
Write-Host "Windows 依赖已下载到虚拟机。"
