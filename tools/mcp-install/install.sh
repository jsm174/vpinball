#!/bin/sh
set -eu

BASE="${VPX_MCP_BASE:-https://github.com/jsm174/vpinball/releases/download/mcp-latest}"
MCP_URL="http://127.0.0.1:2114/mcp"
PLUGIN_BASE="${VPX_WEBVIEW_BASE:-https://github.com/jsm174/vpx-webview-plugin/releases/download/latest}"
PREF="${HOME}/Library/Application Support/VPinballX/10.8"
APP="VPinballX_BGFX.app"

step() {
  printf '\n==> %s\n' "$1"
}

wait_gone() {
  i=0
  while pgrep $1 "$2" > /dev/null 2>&1; do
    i=$((i + 1))
    if [ "${i}" -gt 10 ]; then
      pkill -9 $1 "$2" || true
      break
    fi
    sleep 1
  done
}

if [ "$(uname -s)" != "Darwin" ]; then
  echo "This updater is for macOS. On Windows use install.ps1." >&2
  exit 1
fi

if [ "$(uname -m)" != "arm64" ]; then
  echo "This build is for Apple silicon Macs only." >&2
  exit 1
fi

TMP=$(mktemp -d)
MNT="${TMP}/mnt"
cleanup() {
  hdiutil detach -quiet "${MNT}" > /dev/null 2>&1 || true
  rm -rf "${TMP}"
}
trap cleanup EXIT

VERSION=$(curl -fsSL "${BASE}/version.txt" || echo unknown)
step "Downloading VPinballX ${VERSION}"
curl -fL --progress-bar -o "${TMP}/vpx.dmg" "${BASE}/VPinballX_BGFX-mcp-macos-arm64.dmg"

step "Closing VPinballX"
pkill -f "${APP}/Contents/MacOS" || true
wait_gone -f "${APP}/Contents/MacOS"

DEST="/Applications"
if [ ! -w "${DEST}" ]; then
  DEST="${HOME}/Applications"
  mkdir -p "${DEST}"
fi

step "Installing to ${DEST}/${APP}"
hdiutil attach -nobrowse -readonly -quiet -mountpoint "${MNT}" "${TMP}/vpx.dmg"
rm -rf "${DEST:?}/${APP}"
ditto "${MNT}/${APP}" "${DEST}/${APP}"
hdiutil detach -quiet "${MNT}"

step "Installing the WebView plugin"
curl -fL --progress-bar -o "${TMP}/webview.tar.gz" "${PLUGIN_BASE}/vpx-webview-plugin-macos-arm64.tar.gz"
mkdir -p "${PREF}/plugins"
rm -rf "${PREF}/plugins/webview"
tar xzf "${TMP}/webview.tar.gz" -C "${PREF}/plugins"
INI="${PREF}/VPinballX.ini"
if ! grep -q '^\[Plugin\.WebView\]' "${INI}" 2> /dev/null; then
  printf '\n[Plugin.WebView]\nEnable = 1\n' >> "${INI}"
else
  awk '/^\[/ { inside = ($0 ~ /^\[Plugin\.WebView\]/) } inside && /^Enable[ \t]*=[ \t]*\r?$/ { print "Enable = 1"; next } { print }' "${INI}" > "${TMP}/VPinballX.ini"
  cat "${TMP}/VPinballX.ini" > "${INI}"
fi

step "Starting the live editor"
open -n "${DEST}/${APP}" --args -LiveEdit
i=0
until curl -fs -o /dev/null "http://127.0.0.1:2114/health"; do
  i=$((i + 1))
  if [ "${i}" -gt 60 ]; then
    echo "VPinballX did not answer on port 2114 yet, continuing anyway."
    break
  fi
  sleep 1
done

if [ "${VPX_AGENT:-claude}" = "codex" ]; then
  CODEX=""
  if command -v codex > /dev/null 2>&1; then
    CODEX=$(command -v codex)
  elif [ -x "${HOME}/.local/bin/codex" ]; then
    CODEX="${HOME}/.local/bin/codex"
  else
    step "Installing Codex"
    curl -fsSL https://chatgpt.com/codex/install.sh | sh
    CODEX="${HOME}/.local/bin/codex"
  fi

  step "Connecting Codex to VPinballX"
  "${CODEX}" mcp remove vpx > /dev/null 2>&1 || true
  "${CODEX}" mcp add vpx --url "${MCP_URL}"

  printf '\nDone. VPinballX %s is running and Codex is connected. Start a new Codex session to use it.\n' "${VERSION}"
  exit 0
fi

CLAUDE=""
if command -v claude > /dev/null 2>&1; then
  CLAUDE=$(command -v claude)
elif [ -x "${HOME}/.local/bin/claude" ]; then
  CLAUDE="${HOME}/.local/bin/claude"
else
  step "Installing Claude Code"
  curl -fsSL https://claude.ai/install.sh | bash
  CLAUDE="${HOME}/.local/bin/claude"
fi

step "Connecting Claude to VPinballX"
"${CLAUDE}" mcp remove --scope user vpx > /dev/null 2>&1 || true
"${CLAUDE}" mcp add --transport http --scope user vpx "${MCP_URL}"

if [ -d "/Applications/Claude.app" ]; then
  step "Restarting Claude"
  pkill -x Claude || true
  wait_gone -x Claude
  open -a Claude
fi

printf '\nDone. VPinballX %s is running and Claude is connected.\n' "${VERSION}"
