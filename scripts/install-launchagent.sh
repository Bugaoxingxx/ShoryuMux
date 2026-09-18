#!/usr/bin/env bash
# Install ShoryuMux: build, place the daemon at a stable path, seed config,
# and register the LaunchAgent so it runs at login.
set -euo pipefail

PROJ="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LABEL="com.shoryumux.daemon"
BINDIR="$HOME/Library/Application Support/ShoryuMux"
LOGDIR="$HOME/Library/Logs/ShoryuMux"
CFGDIR="$HOME/.config/shoryumux"
PLIST_DST="$HOME/Library/LaunchAgents/$LABEL.plist"
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

echo "==> Writing LaunchAgent -> $PLIST_DST"
mkdir -p "$(dirname "$PLIST_DST")"
sed -e "s#__BINDIR__#$BINDIR#g" \
    -e "s#__CONFIG__#$CFGDIR/config.conf#g" \
    -e "s#__LOGDIR__#$LOGDIR#g" \
    "$PROJ/launchd/$LABEL.plist.template" > "$PLIST_DST"

echo "==> (Re)loading LaunchAgent"
launchctl bootout "gui/$UID_NUM/$LABEL" 2>/dev/null || true
launchctl bootstrap "gui/$UID_NUM" "$PLIST_DST" 2>/dev/null \
  || launchctl load "$PLIST_DST" 2>/dev/null || true
launchctl kickstart -k "gui/$UID_NUM/$LABEL" 2>/dev/null || true

echo
echo "Done. The daemon now starts at login."
echo "  logs:   $LOGDIR/shoryumuxd.*.log"
echo "  config: $CFGDIR/config.conf"
echo "  stop:   launchctl bootout gui/$UID_NUM/$LABEL"
echo
echo "IMPORTANT: grant Accessibility permission (System Settings -> Privacy &"
echo "Security -> Accessibility) to shoryumuxd, or keystrokes won't be delivered."
echo "Then Stop and Start from the menu-bar app (or: launchctl kickstart -k gui/$UID_NUM/$LABEL)"
