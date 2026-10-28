$base = if ($env:VPX_MCP_BASE) { $env:VPX_MCP_BASE } else { 'https://github.com/jsm174/vpinball/releases/download/mcp-latest' }
$env:VPX_AGENT = 'codex'
try {
    Invoke-RestMethod "$base/install.ps1" | Invoke-Expression
}
finally {
    Remove-Item Env:VPX_AGENT -ErrorAction SilentlyContinue
}
