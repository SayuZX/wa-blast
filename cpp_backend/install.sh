#!/usr/bin/env bash
# ============================================================================
# install.sh — push wa_engine binary + dashboard to a rooted Android device,
# set permissions, and run in background.
#
# Usage:
#   ./install.sh [device_serial]
#   ./install.sh --simulate        # non-root simulation mode
# ============================================================================
set -euo pipefail

ADB="adb"
if [ -n "${1:-}" ] && [ "${1:-}" != "--simulate" ]; then
  ADB="adb -s $1"
fi

SIMULATE=false
if [ "${1:-}" == "--simulate" ] || [ "${2:-}" == "--simulate" ]; then
  SIMULATE=true
fi

BIN="build-android/wa_apid"
DEST="/data/local/tmp/wa-cli"
WWW="dashboard_web"
CFG="config.json"

echo "[1/5] Push binary..."
$ADB push "$BIN" "$DEST/wa_apid"

echo "[2/5] Push config + dashboard..."
$ADB shell "mkdir -p $DEST/dashboard_web"
$ADB push "$CFG" "$DEST/config.json"
$ADB push "$WWW/." "$DEST/dashboard_web/"

echo "[3/5] chmod +x..."
$ADB shell "chmod +x $DEST/wa_apid"

echo "[4/5] Run in background..."
if [ "$SIMULATE" = true ]; then
  $ADB shell "su -c '$DEST/wa_apid --simulate $DEST/config.json' &"
else
  $ADB shell "su -c '$DEST/wa_apid $DEST/config.json' &"
fi

echo "[5/5] Verify..."
sleep 2
$ADB shell "curl -s http://localhost:8080/api/health || echo 'check adb forward tcp:8080 tcp:8080'"

echo
echo "Done. Forward port for browser access:"
echo "  $ADB forward tcp:8080 tcp:8080"
echo "  open http://localhost:8080"
