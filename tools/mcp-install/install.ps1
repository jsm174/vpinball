$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$base = if ($env:VPX_MCP_BASE) { $env:VPX_MCP_BASE } else { 'https://github.com/jsm174/vpinball/releases/download/mcp-latest' }
$mcpUrl = 'http://127.0.0.1:2114/mcp'
$pluginBase = if ($env:VPX_WEBVIEW_BASE) { $env:VPX_WEBVIEW_BASE } else { 'https://github.com/jsm174/vpx-webview-plugin/releases/download/latest' }
$pref = Join-Path $env:APPDATA 'VPinballX\10.8'
$dest = if ($env:VPX_INSTALL_DIR) { $env:VPX_INSTALL_DIR } else { Join-Path $env:LOCALAPPDATA 'Programs\VPinballX-MCP' }
$exeName = 'VPinballX_BGFX64.exe'
$exe = Join-Path $dest $exeName

function Step($text) {
    Write-Host "`n==> $text" -ForegroundColor Cyan
}

[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

$version = 'unknown'
try { $version = (Invoke-RestMethod "$base/version.txt").Trim() } catch { }

$tmp = Join-Path ([IO.Path]::GetTempPath()) ("vpx-mcp-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tmp | Out-Null
try {
    Step "Downloading VPinballX $version"
    $zip = Join-Path $tmp 'vpx.zip'
    Invoke-WebRequest "$base/VPinballX_BGFX-mcp-windows-x64.zip" -OutFile $zip -UseBasicParsing
    $stage = Join-Path $tmp 'stage'
    Expand-Archive -Path $zip -DestinationPath $stage
    if (-not (Test-Path (Join-Path $stage $exeName))) {
        throw "The download does not contain $exeName."
    }

    Step 'Closing VPinballX'
    Get-Process -Name 'VPinballX*' -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Seconds 2

    Step "Installing to $dest"
    if (Test-Path $dest) {
        Remove-Item $dest -Recurse -Force
    }
    New-Item -ItemType Directory -Path (Split-Path $dest -Parent) -Force | Out-Null
    Move-Item $stage $dest

    Step 'Installing the WebView plugin'
    $pluginZip = Join-Path $tmp 'webview.zip'
    Invoke-WebRequest "$pluginBase/vpx-webview-plugin-windows-x64.zip" -OutFile $pluginZip -UseBasicParsing
    $plugins = Join-Path $dest 'plugins'
    if (Test-Path (Join-Path $plugins 'webview')) {
        Remove-Item (Join-Path $plugins 'webview') -Recurse -Force
    }
    Expand-Archive -Path $pluginZip -DestinationPath $plugins
    $ini = Join-Path $pref 'VPinballX.ini'
    if (-not ((Test-Path $ini) -and (Select-String -Path $ini -Pattern '^\[Plugin\.WebView\]' -Quiet))) {
        New-Item -ItemType Directory -Path $pref -Force | Out-Null
        [IO.File]::AppendAllText($ini, "`r`n[Plugin.WebView]`r`nEnable = 1`r`n")
    }
    else {
        $inside = $false
        $lines = foreach ($line in [IO.File]::ReadAllLines($ini)) {
            if ($line -match '^\[') {
                $inside = $line -match '^\[Plugin\.WebView\]'
            }
            if ($inside -and $line -match '^Enable\s*=\s*$') { 'Enable = 1' } else { $line }
        }
        [IO.File]::WriteAllLines($ini, [string[]]$lines)
    }
}
finally {
    Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
}

$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut((Join-Path ([Environment]::GetFolderPath('Desktop')) 'VPinballX Live Editor.lnk'))
$shortcut.TargetPath = $exe
$shortcut.Arguments = '-LiveEdit'
$shortcut.WorkingDirectory = $dest
$shortcut.Save()

Step 'Starting the live editor'
Start-Process $exe -ArgumentList '-LiveEdit' -WorkingDirectory $dest
$ready = $false
for ($i = 0; $i -lt 60 -and -not $ready; $i++) {
    try {
        Invoke-RestMethod 'http://127.0.0.1:2114/health' -TimeoutSec 2 | Out-Null
        $ready = $true
    }
    catch {
        Start-Sleep -Seconds 1
    }
}
if (-not $ready) {
    Write-Host 'VPinballX did not answer on port 2114 yet, continuing anyway.' -ForegroundColor Yellow
}

if ($env:VPX_AGENT -eq 'codex') {
    $localCodex = Join-Path $env:LOCALAPPDATA 'Programs\OpenAI\Codex\bin\codex.exe'
    $codex = $null
    $command = Get-Command codex -ErrorAction SilentlyContinue
    if ($command) {
        $codex = $command.Source
    }
    elseif (Test-Path $localCodex) {
        $codex = $localCodex
    }
    else {
        Step 'Installing Codex'
        powershell -NoProfile -ExecutionPolicy Bypass -Command "irm https://chatgpt.com/codex/install.ps1 | iex"
        $codex = $localCodex
    }

    Step 'Connecting Codex to VPinballX'
    $ErrorActionPreference = 'Continue'
    & $codex mcp remove vpx 2>&1 | Out-Null
    & $codex mcp add vpx --url $mcpUrl
    $ErrorActionPreference = 'Stop'

    Write-Host "`nDone. VPinballX $version is running and Codex is connected. Start a new Codex session to use it." -ForegroundColor Green
    return
}

$localClaude = Join-Path $env:USERPROFILE '.local\bin\claude.exe'
$claude = $null
$command = Get-Command claude -ErrorAction SilentlyContinue
if ($command) {
    $claude = $command.Source
}
elseif (Test-Path $localClaude) {
    $claude = $localClaude
}
else {
    Step 'Installing Claude Code'
    powershell -NoProfile -ExecutionPolicy Bypass -Command "irm https://claude.ai/install.ps1 | iex"
    $claude = $localClaude
}

Step 'Connecting Claude to VPinballX'
$ErrorActionPreference = 'Continue'
& $claude mcp remove --scope user vpx 2>&1 | Out-Null
& $claude mcp add --transport http --scope user vpx $mcpUrl
$ErrorActionPreference = 'Stop'

$desktopApp = Get-StartApps | Where-Object { $_.Name -eq 'Claude' } | Select-Object -First 1
if ($desktopApp) {
    Step 'Restarting Claude'
    Get-Process -Name 'claude' -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -and $_.Path -match 'AnthropicClaude|WindowsApps' } |
        Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2
    Start-Process "shell:AppsFolder\$($desktopApp.AppID)"
}

Write-Host "`nDone. VPinballX $version is running and Claude is connected." -ForegroundColor Green
