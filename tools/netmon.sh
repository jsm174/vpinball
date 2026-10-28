#!/bin/bash

set -uo pipefail

PROC_MATCH="VPinballX"
INTERVAL=1
LOGFILE=""
SNIFF=0
SHOW_LISTEN=1
QUIET_DNS=0

usage() {
  cat <<EOF
usage: netmon.sh [options]

Monitors every outgoing network connection made by the VPinball process
(and anything running inside it: plugins, PuP, B2S, Scorbit, dmdserver...).

options:
  -p, --process NAME   process name to match (default: $PROC_MATCH)
      --pid PID        watch this pid only
  -i, --interval SEC   poll interval (default: $INTERVAL)
  -l, --log FILE       append output to FILE as well as stdout
  -s, --sniff          also sniff DNS to resolve IPs to the hostnames the
                       app actually asked for (needs sudo)
      --no-listen      hide listening sockets, show outbound only
  -h, --help           this

examples:
  ./tools/netmon.sh
  sudo ./tools/netmon.sh --sniff --log /tmp/vpx-net.log
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -p|--process) PROC_MATCH="$2"; shift 2 ;;
    --pid) FORCE_PID="$2"; shift 2 ;;
    -i|--interval) INTERVAL="$2"; shift 2 ;;
    -l|--log) LOGFILE="$2"; shift 2 ;;
    -s|--sniff) SNIFF=1; shift ;;
    --no-listen) SHOW_LISTEN=0; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage; exit 1 ;;
  esac
done

TMPDIR_RUN="$(mktemp -d "${TMPDIR:-/tmp}/vpxnetmon.XXXXXX")"
DNSMAP="$TMPDIR_RUN/dnsmap"
SEEN="$TMPDIR_RUN/seen"
RDNS="$TMPDIR_RUN/rdns"
: > "$DNSMAP"; : > "$SEEN"; : > "$RDNS"
TCPDUMP_PID=""
PARSER_PID=""

C_RESET=$'\033[0m'; C_DIM=$'\033[2m'; C_NEW=$'\033[1;32m'
C_END=$'\033[0;33m'; C_LIS=$'\033[1;36m'; C_HDR=$'\033[1;35m'
if [[ ! -t 1 ]]; then C_RESET=""; C_DIM=""; C_NEW=""; C_END=""; C_LIS=""; C_HDR=""; fi

emit() {
  local line="$1"
  printf '%s\n' "$line"
  [[ -n "$LOGFILE" ]] && printf '%s\n' "$(printf '%s' "$line" | sed $'s/\033\\[[0-9;]*m//g')" >> "$LOGFILE"
}

cleanup() {
  [[ -n "$TCPDUMP_PID" ]] && kill "$TCPDUMP_PID" 2>/dev/null
  [[ -n "$PARSER_PID" ]] && { pkill -P "$PARSER_PID" 2>/dev/null; kill "$PARSER_PID" 2>/dev/null; }
  emit ""
  emit "${C_HDR}=== summary: every destination contacted ===${C_RESET}"
  if [[ -s "$SEEN" ]]; then
    sort -u "$SEEN" | while IFS='|' read -r proto remote _; do
      [[ "$remote" == *"*"* ]] && continue
      local ip="${remote%:*}"; ip="${ip#[}"; ip="${ip%]}"
      emit "$(printf '  %-5s %-46s %s' "$proto" "$remote" "$(label_for "$ip")")"
    done
  else
    emit "  (no outbound connections observed)"
  fi
  rm -rf "$TMPDIR_RUN"
  exit 0
}

lookup_cache() {
  grep -m1 "^$1	" "$2" 2>/dev/null | cut -f2-
}

label_for() {
  local ip="$1" name
  name="$(lookup_cache "$ip" "$DNSMAP")"
  if [[ -n "$name" ]]; then printf '%s' "$name"; return; fi
  name="$(lookup_cache "$ip" "$RDNS")"
  if [[ -z "$name" ]]; then
    name="$(dig +short +time=1 +tries=1 -x "$ip" 2>/dev/null | head -1 | sed 's/\.$//')"
    [[ -z "$name" ]] && name="-"
    printf '%s\t%s\n' "$ip" "$name" >> "$RDNS"
  fi
  [[ "$name" == "-" ]] && printf '' || printf '%s' "$name"
}

svc_for() {
  case "$1" in
    80) echo http ;; 443) echo "https/tls" ;; 53) echo dns ;;
    22) echo ssh ;; 21) echo ftp ;; 123) echo ntp ;;
    5353) echo mdns ;; 1900) echo ssdp ;; 8080|8081) echo http-alt ;;
    *) echo "" ;;
  esac
}

start_sniffer() {
  if [[ "$(id -u)" != "0" ]]; then
    emit "${C_DIM}--sniff needs root; re-run with sudo. continuing without hostname capture.${C_RESET}"
    return
  fi
  : > "$TMPDIR_RUN/dns.raw"
  tcpdump -n -l -q -s 0 'udp port 53' > "$TMPDIR_RUN/dns.raw" 2>/dev/null &
  TCPDUMP_PID=$!
  tail -f "$TMPDIR_RUN/dns.raw" 2>/dev/null | awk -v out="$DNSMAP" '
    { for (i = 1; i <= NF; i++) {
        if ($i == "A?" || $i == "AAAA?") { id = $(i-1); gsub(/[^0-9]/, "", id)
          nm = $(i+1); sub(/\.$/, "", nm); sub(/,$/, "", nm); q[id] = nm }
        else if (($i == "A" || $i == "AAAA") && (i + 1) <= NF) {
          id = $6; gsub(/[^0-9]/, "", id)
          ip = $(i+1); sub(/,$/, "", ip)
          if (id in q && ip ~ /^[0-9a-fA-F:.]+$/ && !(ip SUBSEP q[id] in done)) {
            done[ip SUBSEP q[id]] = 1
            printf "%s\t%s\n", ip, q[id] >> out; fflush(out) } } } }' &
  PARSER_PID=$!
  emit "${C_DIM}dns sniffer running (system-wide; macOS resolves via mDNSResponder)${C_RESET}"
}

get_pids() {
  if [[ -n "${FORCE_PID:-}" ]]; then echo "$FORCE_PID"; return; fi
  pgrep -f "$PROC_MATCH" | tr '\n' ' '
}

snapshot() {
  local pidcsv="$1"
  lsof -nP -i -a -p "$pidcsv" -FpcnPT 2>/dev/null | awk '
    /^p/ { pid = substr($0, 2); next }
    /^c/ { cmd = substr($0, 2); next }
    /^P/ { proto = substr($0, 2); next }
    /^n/ { name = substr($0, 2); state = ""; next }
    /^TST=/ { state = substr($0, 5)
      printf "%s|%s|%s|%s|%s\n", proto, name, state, cmd, pid; name = ""; next }
    END { }' 
}

trap cleanup INT TERM

emit "${C_HDR}vpx netmon${C_RESET} ${C_DIM}watching '$PROC_MATCH' every ${INTERVAL}s — ctrl-c to stop${C_RESET}"
[[ -n "$LOGFILE" ]] && emit "${C_DIM}logging to $LOGFILE${C_RESET}"
[[ "$SNIFF" == "1" ]] && start_sniffer

LAST_PIDS=""
while true; do
  PIDS="$(get_pids)"
  if [[ -z "${PIDS// /}" ]]; then
    if [[ -n "$LAST_PIDS" ]]; then
      emit "$(printf '%s[%s] process exited%s' "$C_DIM" "$(date '+%H:%M:%S')" "$C_RESET")"
      LAST_PIDS=""
    fi
    sleep "$INTERVAL"
    continue
  fi
  if [[ "$PIDS" != "$LAST_PIDS" ]]; then
    emit "$(printf '%s[%s] attached to pid(s): %s%s' "$C_DIM" "$(date '+%H:%M:%S')" "${PIDS% }" "$C_RESET")"
    LAST_PIDS="$PIDS"
  fi

  CUR="$TMPDIR_RUN/cur"; : > "$CUR"
  snapshot "$(echo "${PIDS% }" | tr ' ' ',')" > "$CUR"
  awk -F'|' '{ printf "%s|%s|%s\n", $1, $2, $3 }' "$CUR" > "$CUR.keys"

  while IFS='|' read -r proto name state cmd pid; do
    [[ -z "$name" ]] && continue
    key="$proto|$name|$state"
    grep -qxF "$key" "$TMPDIR_RUN/live" 2>/dev/null && continue

    if [[ "$name" == *"->"* ]]; then
      remote="${name##*->}"
      ip="${remote%:*}"; ip="${ip#[}"; ip="${ip%]}"
      port="${remote##*:}"
      host="$(label_for "$ip")"
      svc="$(svc_for "$port")"
      detail="$host"
      [[ -n "$svc" ]] && detail="${detail:+$detail }${C_DIM}($svc)${C_RESET}"
      emit "$(printf '%s[%s] OUT %s %-5s %-44s %s  %s(%s/%s)%s' \
        "$C_NEW" "$(date '+%H:%M:%S')" "$C_RESET" "$proto" "$remote" "$detail" "$C_DIM" "$cmd" "$pid" "$C_RESET")"
      printf '%s|%s|%s\n' "$proto" "$remote" "$state" >> "$SEEN"
    elif [[ "$SHOW_LISTEN" == "1" ]]; then
      port="${name##*:}"
      svc="$(svc_for "$port")"
      emit "$(printf '%s[%s] LISTEN%s %-5s %-44s %s%s(%s/%s)%s' \
        "$C_LIS" "$(date '+%H:%M:%S')" "$C_RESET" "$proto" "$name" "${svc:+$svc  }" "$C_DIM" "$cmd" "$pid" "$C_RESET")"
    fi
  done < "$CUR"

  if [[ -f "$TMPDIR_RUN/live" ]]; then
    while IFS='|' read -r proto name state; do
      [[ "$name" == *"->"* ]] || continue
      grep -qxF "$proto|$name|$state" "$CUR.keys" 2>/dev/null && continue
      emit "$(printf '%s[%s] CLOSED %s %-5s %s' "$C_END" "$(date '+%H:%M:%S')" "$C_RESET" "$proto" "${name##*->}")"
    done < <(grep -- '->' "$TMPDIR_RUN/live" 2>/dev/null || true)
  fi

  cp "$CUR.keys" "$TMPDIR_RUN/live"

  sleep "$INTERVAL"
done
