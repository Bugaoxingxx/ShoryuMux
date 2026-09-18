#!/usr/bin/env bash
# Uninstall the ShoryuMux LaunchAgent (keeps your config by default).
set -euo pipefail

LABEL="com.shoryumux.daemon"
PLIST_DST="$HOME/Library/LaunchAgents/$LABEL.plist"
BINDIR="$HOME/Library/Application Support/ShoryuMux"
UID_NUM="$(id -u)"

echo "==> Stopping + removing LaunchAgent"
launchctl bootout "gui/$UID_NUM/$LABEL" 2>/dev/null || true
rm -f "$PLIST_DST"

if [ "${1:-}" = "--purge" ]; then
  echo "==> Purging binary and logs"
  rm -f "$BINDIR/shoryumuxd"
  rm -rf "$HOME/Library/Logs/ShoryuMux"
  echo "    (config at ~/.config/shoryumux kept; remove manually if desired)"
else
  echo "==> Binary left at $BINDIR/shoryumuxd (pass --purge to remove)"
fi
echo "Uninstalled."
