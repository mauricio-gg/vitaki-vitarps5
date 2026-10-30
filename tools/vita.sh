#!/usr/bin/env bash
# Talk to the test Vita over vitacompanion (FTP 1337, command port 1338).
# The Vita address comes from the VITA_IP environment variable.
#
#   tools/vita.sh upload <local.vpk> [remote-name]   -> ux0:/vpk/<remote-name>
#   tools/vita.sh ls [dir]                           -> list ux0:/<dir> (default vpk)
#   tools/vita.sh log <local-path>                   -> pull the testing log
#   tools/vita.sh cmd <vitacompanion command...>     -> e.g. cmd version
set -u

FTP_PORT=1337
CMD_PORT=1338
LOG_REMOTE="ux0:data/vita-chiaki/vitarps5-testing.log"

die() { echo "vita.sh: $*" >&2; exit 1; }

[ -n "${VITA_IP:-}" ] || die "VITA_IP is not set. Export the Vita's address first, e.g. 'export VITA_IP=192.168.1.50' (Vita must be awake with vitacompanion running)."

# Curl against the Vita's FTP server; fails loudly with a message naming the IP.
ftp() {
  local out rc
  out=$(curl -sS --fail --connect-timeout 3 --max-time 600 "$@" 2>&1); rc=$?
  if [ $rc -ne 0 ]; then
    echo "$out" >&2
    die "FTP request to $VITA_IP:$FTP_PORT failed (curl exit $rc). Is the Vita awake, on the same network, and running vitacompanion?"
  fi
  [ -z "$out" ] || printf '%s\n' "$out"
}

sub="${1:-}"; shift || true
case "$sub" in
  upload)
    src="${1:-}"; [ -f "$src" ] || die "usage: upload <local.vpk> [remote-name] (file not found: '$src')"
    name="${2:-$(basename "$src")}"
    ftp -T "$src" "ftp://$VITA_IP:$FTP_PORT/ux0:/vpk/$name"
    echo "uploaded $src -> ux0:/vpk/$name ($(wc -c <"$src" | tr -d ' ') bytes)"
    ;;
  ls)
    ftp --list-only "ftp://$VITA_IP:$FTP_PORT/ux0:/${1:-vpk}/" 
    ;;
  log)
    dest="${1:-}"; [ -n "$dest" ] || die "usage: log <local-path>"
    ftp --ftp-method nocwd -o "$dest" "ftp://$VITA_IP:$FTP_PORT//$LOG_REMOTE" \
      || exit 1
    echo "pulled $LOG_REMOTE -> $dest ($(wc -c <"$dest" | tr -d ' ') bytes)"
    ;;
  cmd)
    [ $# -gt 0 ] || die "usage: cmd <vitacompanion command...> (see the skill for the list)"
    printf '%s\n' "$*" | nc -w 3 "$VITA_IP" "$CMD_PORT" \
      || die "command port $VITA_IP:$CMD_PORT did not answer. Is vitacompanion running?"
    ;;
  *)
    die "usage: vita.sh {upload <vpk> [name] | ls [dir] | log <path> | cmd <command...>}"
    ;;
esac
