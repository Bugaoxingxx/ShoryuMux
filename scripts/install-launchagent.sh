#!/usr/bin/env bash
# Install ShoryuMux daemon binary + default config.
# The menu-bar app owns the daemon (starts/stops with it). This script does
# NOT register a LaunchAgent for shoryumuxd — use the app's "Install at login"
# to open ShoryuMux.app at login.
set -euo pipefail

PROJ="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OLD_LABEL="com.shoryumux.daemon"
BINDIR="$HOME/Library/Application Support/ShoryuMux"
LOGDIR="$HOME/Library/Logs/ShoryuMux"
CFGDIR="$HOME/.config/shoryumux"
PLIST_DST="$HOME/Library/LaunchAgents/$OLD_LABEL.plist"
UID_NUM="$(id -u)"

echo "==> Building core daemon"
( cd "$PROJ" && make core )

echo "==> Installing binary -> $BINDIR/shoryumuxd"
mkdir -p "$BINDIR" "$LOGDIR" "$CFGDIR"
cp "$PROJ/build/shoryumuxd" "$BINDIR/shoryumuxd"

if [ ! -f "$CFGDIR/config.conf" ]; then
  cp "$PROJ/core/config.conf.default" "$CFGDIR/config.conf"
  echo "==> Seeded default config -> $CFGDIR/config.conf"
else
  echo "==> Config exists, left untouched -> $CFGDIR/config.conf"
fi

echo "==> Removing old unbound daemon LaunchAgent (if any)"
launchctl bootout "gui/$UID_NUM/$OLD_LABEL" 2>/dev/null || true
rm -f "$PLIST_DST"

echo
echo "Done. Open the menu-bar app to start the daemon:"
echo "  open \"$PROJ/build/ShoryuMux.app\""
echo "  logs:   $LOGDIR/shoryumuxd.*.log"
echo "  config: $CFGDIR/config.conf"
echo
echo "Login item is the App (not the daemon). In the menu bar: Install at login."
echo "IMPORTANT: grant Accessibility to $BINDIR/shoryumuxd, then Stop and Start."
