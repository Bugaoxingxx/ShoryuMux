#!/usr/bin/env bash
# Remove leftover daemon LaunchAgent and optionally the installed binary.
# App login item is unregistered from the menu-bar "Uninstall at login".
set -euo pipefail

OLD_LABEL="com.shoryumux.daemon"
PLIST_DST="$HOME/Library/LaunchAgents/$OLD_LABEL.plist"
BINDIR="$HOME/Library/Application Support/ShoryuMux"
UID_NUM="$(id -u)"

echo "==> Stopping leftover daemon LaunchAgent (if any)"
launchctl bootout "gui/$UID_NUM/$OLD_LABEL" 2>/dev/null || true
rm -f "$PLIST_DST"

if [ "${1:-}" = "--purge" ]; then
  echo "==> Purging binary and logs"
  rm -f "$BINDIR/shoryumuxd"
  rm -rf "$HOME/Library/Logs/ShoryuMux"
  echo "    (config at ~/.config/shoryumux kept; remove manually if desired)"
else
  echo "==> Binary left at $BINDIR/shoryumuxd (pass --purge to remove)"
fi
echo "Uninstalled leftover LaunchAgent."
