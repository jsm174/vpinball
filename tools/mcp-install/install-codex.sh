#!/bin/sh
set -eu

BASE="${VPX_MCP_BASE:-https://github.com/jsm174/vpinball/releases/download/mcp-latest}"
curl -fsSL "${BASE}/install.sh" | VPX_AGENT=codex sh
