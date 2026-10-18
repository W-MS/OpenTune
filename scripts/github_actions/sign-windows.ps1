# Sign the Windows executables that the portable package will publish.
# The signer cannot read and write the same path, so each exe is signed to a
# temporary file and then moved over the unsigned original.
# This file is ASCII on purpose. Windows PowerShell 5.1 parses scripts with
# the system ANSI code page unless the file has a UTF-8 BOM.
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Script:Latin1 = [Text.Encoding]::GetEncoding(28591)

function Register-GitHubMask {
    param([string]$Secret)
    if ([string]::IsNullOrWhiteSpace($Secret)) { return }
    if ($env:GITHUB_ACTIONS -eq "true") {
        Write-Host "::add-mask::$Secret"
    }
}

function Format-WinArgument {
    param([string]$Text)
    '"' + ($Text -replace '"', '\"') + '"'
}

function Get-CodePointText {
    param([int[]]$CodePoints)
    -join ($CodePoints | ForEach-Object { [char]$_ })
}

function New-BytePattern {
    param([int[]]$Values)
    $bytes = New-Object byte[] $Values.Length
    for ($i = 0; $i -lt $Values.Length; $i++) {
        $bytes[$i] = [byte]$Values[$i]
    }
    return ,$bytes
}

# Compare captured stdout/stderr as raw bytes. Latin-1 keeps every byte, so a
# GBK, UTF-8, or UTF-16 message still matches after the pipe is redirected.
function Test-RawContains {
    param(
        [AllowEmptyString()]
        [string]$Raw,
        [byte[]]$Pattern
    )
    if ([string]::IsNullOrEmpty($Raw) -or $null -eq $Pattern -or $Pattern.Length -eq 0) {
        return $false
    }
    $needle = $Script:Latin1.GetString($Pattern)
    return $Raw.Contains($needle)
}

function Test-TextOccurs {
    param(
        [AllowEmptyString()]
        [string]$Raw,
        [string]$Text,
        [byte[]]$GbkPattern
    )
    if ([string]::IsNullOrEmpty($Raw) -or [string]::IsNullOrEmpty($Text)) {
        return $false
    }
    # Keep each encoding in its own variable. An array of byte arrays is
    # flattened by PowerShell before it can be searched.
    $utf8 = [Text.Encoding]::UTF8.GetBytes($Text)
    $utf16 = [Text.Encoding]::Unicode.GetBytes($Text)
    $utf16Be = [Text.Encoding]::BigEndianUnicode.GetBytes($Text)
    if (Test-RawContains -Raw $Raw -Pattern $utf8) { return $true }
    if (Test-RawContains -Raw $Raw -Pattern $utf16) { return $true }
    if (Test-RawContains -Raw $Raw -Pattern $utf16Be) { return $true }
    if ($null -ne $GbkPattern -and (Test-RawContains -Raw $Raw -Pattern $GbkPattern)) {
        return $true
    }
    return $false
}

function Invoke-Captured {
    param(
        [string]$FileName,
        [string[]]$ArgumentList
    )
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $FileName
    $quoted = foreach ($arg in $ArgumentList) { Format-WinArgument $arg }
    $psi.Arguments = $quoted -join " "
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true
    $psi.StandardOutputEncoding = $Script:Latin1
    $psi.StandardErrorEncoding = $Script:Latin1
    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi
    if (-not $proc.Start()) {
        throw "Could not start $([IO.Path]::GetFileName($FileName))"
    }
    try {
        $stdoutTask = $proc.StandardOutput.ReadToEndAsync()
        $stderrTask = $proc.StandardError.ReadToEndAsync()
        $proc.WaitForExit()
        $stdoutTask.Wait()
        $stderrTask.Wait()
        $stdout = $stdoutTask.Result
        $stderr = $stderrTask.Result
        if ($null -eq $stdout) { $stdout = "" }
        if ($null -eq $stderr) { $stderr = "" }
        [pscustomobject]@{
            ExitCode = $proc.ExitCode
            Raw      = $stdout + $stderr
        }
    }
    finally {
        $proc.Dispose()
    }
}

$Chap = $env:WIN_SIGNER_CHAP
$Endpoint = $env:WIN_SIGNER_ENDPOINT
$Key = $env:WIN_SIGNER_KEY
$Url = $env:WIN_SIGNER_URL
foreach ($name in @("WIN_SIGNER_CHAP", "WIN_SIGNER_ENDPOINT", "WIN_SIGNER_KEY", "WIN_SIGNER_URL")) {
    if ([string]::IsNullOrWhiteSpace((Get-Item -Path "Env:$name" -ErrorAction SilentlyContinue).Value)) {
        throw "Missing secret: $name"
    }
}

$EndpointHost = ($Endpoint -split ":", 2)[0]
$ChapUser = ($Chap -split "\s+", 2)[0]
$ChapSecret = ""
if ($Chap -match "\s+(\S.*)$") { $ChapSecret = $Matches[1] }
$ConnectArg = "$Key@$Endpoint"
foreach ($secret in @($Chap, $ChapUser, $ChapSecret, $Endpoint, $EndpointHost, $Key, $Url, $ConnectArg)) {
    Register-GitHubMask $secret
}

# Phrases the signer prints. Code points and GBK bytes stay ASCII in this file.
# connected:    U+5DF2 U+8FDE U+63A5    GBK D2 D1 C1 AC BD D3
# logged in:    U+5DF2 U+767B U+5F55    GBK D2 D1 B5 C7 C2 BC
# auth failed:  U+8BA4 U+8BC1 U+5931 U+8D25    GBK C8 CF D6 A4 CA A7 B0 DC
# signed:       U+5DF2 U+7B7E U+540D    GBK D2 D1 C7 A9 C3 FB
$ConnectedText = Get-CodePointText 0x5DF2, 0x8FDE, 0x63A5
$LoggedInText = Get-CodePointText 0x5DF2, 0x767B, 0x5F55
$AuthFailedText = Get-CodePointText 0x8BA4, 0x8BC1, 0x5931, 0x8D25
$SignedText = Get-CodePointText 0x5DF2, 0x7B7E, 0x540D
$ConnectedGbk = New-BytePattern 0xD2, 0xD1, 0xC1, 0xAC, 0xBD, 0xD3
$LoggedInGbk = New-BytePattern 0xD2, 0xD1, 0xB5, 0xC7, 0xC2, 0xBC
$AuthFailedGbk = New-BytePattern 0xC8, 0xCF, 0xD6, 0xA4, 0xCA, 0xA7, 0xB0, 0xDC
$SignedGbk = New-BytePattern 0xD2, 0xD1, 0xC7, 0xA9, 0xC3, 0xFB

$Standalone = Join-Path $Root "build-ara-overlay-vs18-clean\OpenTuneStandalone_artefacts\Release\Standalone"
$Executables = @(Get-ChildItem -LiteralPath $Standalone -Filter "*.exe" -File -Recurse -ErrorAction SilentlyContinue)
if ($Executables.Count -eq 0) {
    throw "No Windows executable was found to sign"
}

$Work = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { $env:TEMP }
$Signer = Join-Path $Work "wmssign.exe"
if (Test-Path -LiteralPath $Signer) {
    Remove-Item -LiteralPath $Signer -Force
}

try {
    Write-Host "Downloading the Windows signer"
    $download = Invoke-Captured "curl.exe" @("-fsSL", "--retry", "5", "--retry-delay", "2", "-o", $Signer, $Url)
    if ($download.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $Signer)) {
        throw "Could not download the Windows signer"
    }

    Write-Host "Connecting to the Windows signer"
    $connected = Invoke-Captured $Signer @("connect", $ConnectArg)
    $authFailed = Test-TextOccurs -Raw $connected.Raw -Text $AuthFailedText -GbkPattern $AuthFailedGbk
    $connectOk = $connected.ExitCode -eq 0 -and -not $authFailed -and (Test-TextOccurs -Raw $connected.Raw -Text $ConnectedText -GbkPattern $ConnectedGbk)
    if (-not $connectOk) {
        throw "Authentication failed"
    }
    Write-Host "Connected"

    Write-Host "Logging in to the Windows signer"
    $loggedIn = Invoke-Captured $Signer @("login", $Chap)
    if ($loggedIn.ExitCode -ne 0 -or -not (Test-TextOccurs -Raw $loggedIn.Raw -Text $LoggedInText -GbkPattern $LoggedInGbk)) {
        throw "Login failed"
    }
    Write-Host "Logged in"

    foreach ($exe in $Executables) {
        $signed = Join-Path $Work ("signed-" + [guid]::NewGuid().ToString("n") + ".exe")
        $signedName = [IO.Path]::GetFileName($signed)
        Write-Host "Signing $($exe.Name)"
        try {
            $result = Invoke-Captured $Signer @("sign", $exe.FullName, $signed)
            $outputReady = (Test-Path -LiteralPath $signed) -and ((Get-Item -LiteralPath $signed).Length -gt 0)
            $signedPhrase = Test-TextOccurs -Raw $result.Raw -Text $SignedText -GbkPattern $SignedGbk
            $namedOutput = (Test-TextOccurs -Raw $result.Raw -Text $signed) -or (Test-TextOccurs -Raw $result.Raw -Text $signedName)
            if (-not $outputReady -or -not ($signedPhrase -or $namedOutput)) {
                throw "Could not sign $($exe.Name)"
            }
            Move-Item -LiteralPath $signed -Destination $exe.FullName -Force
            Write-Host "Signed $($exe.Name)"
        }
        finally {
            if (Test-Path -LiteralPath $signed) {
                Remove-Item -LiteralPath $signed -Force
            }
        }
    }
}
finally {
    if (Test-Path -LiteralPath $Signer) {
        Remove-Item -LiteralPath $Signer -Force
    }
}

Write-Host "Windows executables are signed."
