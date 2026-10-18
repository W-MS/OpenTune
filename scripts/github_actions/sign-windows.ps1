# Sign the Windows executables that the portable package will publish.
# Secrets stay in the process environment. Tool output is redacted before it is logged.
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

function Register-GitHubMask {
    param([string]$Secret)
    if ([string]::IsNullOrWhiteSpace($Secret)) { return }
    if ($env:GITHUB_ACTIONS -eq "true") {
        Write-Host "::add-mask::$Secret"
    }
}

function Hide-SignerOutput {
    param(
        [string]$Text,
        [string[]]$Secrets
    )
    if ($null -eq $Text) { return "" }
    $hidden = $Text
    foreach ($secret in ($Secrets | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Sort-Object Length -Descending)) {
        $hidden = $hidden.Replace($secret, "******")
    }
    $hidden = [regex]::Replace($hidden, "已连接（[^）]*）", "已连接（域名请隐藏）")
    $hidden = [regex]::Replace($hidden, "证书指纹\s*[:：]\s*[^，,\r\n]+", "证书指纹：（请隐藏）")
    $hidden = [regex]::Replace($hidden, "已登录（[^）]*）", "已登录（用户名请隐藏）")
    $hidden = [regex]::Replace($hidden, "(?i)([0-9a-f]{2}:){7,}[0-9a-f]{2}", "******")
    $hidden = [regex]::Replace($hidden, "(?i)\b[0-9a-f]{40,}\b", "******")
    $hidden = [regex]::Replace($hidden, "(?i)https?://\S+", "******")
    return $hidden
}

function Format-WinArgument {
    param([string]$Text)
    '"' + ($Text -replace '"', '\"') + '"'
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
    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi
    if (-not $proc.Start()) {
        throw "Could not start $([IO.Path]::GetFileName($FileName))"
    }
    $stdoutTask = $proc.StandardOutput.ReadToEndAsync()
    $stderrTask = $proc.StandardError.ReadToEndAsync()
    $proc.WaitForExit()
    $stdoutTask.Wait()
    $stderrTask.Wait()
    [pscustomobject]@{
        ExitCode = $proc.ExitCode
        Output   = $stdoutTask.Result + $stderrTask.Result
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
$RedactSecrets = @($Chap, $ChapUser, $ChapSecret, $Endpoint, $EndpointHost, $Key, $Url, $ConnectArg)

$Standalone = Join-Path $Root "build-ara-overlay-vs18-clean\OpenTuneStandalone_artefacts\Release\Standalone"
$Executables = @(Get-ChildItem -LiteralPath $Standalone -Filter "*.exe" -File -Recurse -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -notlike "*.wmssigned.exe" })
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
    $downloadText = Hide-SignerOutput -Text $download.Output -Secrets $RedactSecrets
    if ($download.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $Signer)) {
        if (-not [string]::IsNullOrWhiteSpace($downloadText)) { Write-Host $downloadText }
        throw "Could not download the Windows signer"
    }

    Write-Host "Connecting to the Windows signer"
    $connected = Invoke-Captured $Signer @("connect", $ConnectArg)
    $connectedText = Hide-SignerOutput -Text $connected.Output -Secrets $RedactSecrets
    if (-not [string]::IsNullOrWhiteSpace($connectedText)) { Write-Host $connectedText.Trim() }
    if ($connected.ExitCode -ne 0 -or $connected.Output -match "认证失败" -or $connectedText -notmatch "已连接") {
        throw "错误：认证失败"
    }

    Write-Host "Logging in to the Windows signer"
    $loggedIn = Invoke-Captured $Signer @("login", $Chap)
    $loggedInText = Hide-SignerOutput -Text $loggedIn.Output -Secrets $RedactSecrets
    if (-not [string]::IsNullOrWhiteSpace($loggedInText)) { Write-Host $loggedInText.Trim() }
    if ($loggedIn.ExitCode -ne 0 -or $loggedInText -notmatch "已登录") {
        throw "Windows signer login failed"
    }

    foreach ($exe in $Executables) {
        $signed = Join-Path $exe.DirectoryName ($exe.BaseName + ".wmssigned.exe")
        if (Test-Path -LiteralPath $signed) {
            Remove-Item -LiteralPath $signed -Force
        }
        Write-Host "Signing $($exe.Name)"
        $result = Invoke-Captured $Signer @("sign", $exe.FullName, $signed)
        $resultText = Hide-SignerOutput -Text $result.Output -Secrets $RedactSecrets
        if (-not [string]::IsNullOrWhiteSpace($resultText)) { Write-Host $resultText.Trim() }
        if ($result.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $signed) -or (Get-Item -LiteralPath $signed).Length -eq 0) {
            if (Test-Path -LiteralPath $signed) { Remove-Item -LiteralPath $signed -Force }
            throw "Could not sign $($exe.Name)"
        }
        Move-Item -LiteralPath $signed -Destination $exe.FullName -Force
    }
}
finally {
    if (Test-Path -LiteralPath $Signer) {
        Remove-Item -LiteralPath $Signer -Force
    }
}

Write-Host "Windows executables are signed."
