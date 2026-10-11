#!/bin/bash
#
# Re-signs LibreSprite with the free personal team and installs it on the iPad.
#
#   ./ios/resign-install.sh [--launch]
#
# Free-team provisioning profiles expire after 7 days, after which the app
# will not start. Running this refreshes the profile (xcodebuild
# -allowProvisioningUpdates) and reinstalls the app; the app's data survives a
# reinstall. The iPad must be unlocked, paired, and on the same network (or
# plugged in). See ios/README.md for the first-time setup of build/ios-xcode.

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEVICE="${LIBRESPRITE_DEVICE:-00008122-000468320251801C}"
BUNDLE_ID="${LIBRESPRITE_BUNDLE_IDENTIFIER:-com.mooz.LibreSprite}"
PROJECT="$REPO/build/ios-xcode/libresprite.xcodeproj"
APP="$REPO/build/ios-xcode/bin/libresprite.app"

if ! xcrun devicectl list devices 2>/dev/null | grep -q "$DEVICE.*\(available\|connected\)"; then
  echo "iPad $DEVICE is not reachable: unlock it and check it is on the same network" >&2
  exit 1
fi

# xcodebuild exits 0 on some failures, so look for the success marker.
LOG="$(mktemp)"
xcodebuild -project "$PROJECT" -scheme libresprite -configuration Release \
  -destination 'generic/platform=iOS' -allowProvisioningUpdates build >"$LOG" 2>&1 || true
if ! grep -q '\*\* BUILD SUCCEEDED \*\*' "$LOG"; then
  grep -E 'error:' "$LOG" | head >&2
  echo "build failed, full log: $LOG" >&2
  exit 1
fi
rm -f "$LOG"

xcrun devicectl device install app --device "$DEVICE" "$APP"
[ "${1:-}" = "--launch" ] && xcrun devicectl device process launch --terminate-existing --device "$DEVICE" "$BUNDLE_ID"
echo "Installed. The new profile is valid for 7 days."
