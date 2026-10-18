#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
PACKAGE=${1:-"$ROOT/metamodule/metamodule-plugins/SpaceTime.mmplugin"}
VOLUME=${2:-/Volumes/DEV_MM}
DEVICE=${3:-}
TIMEOUT=${INSTALL_TIMEOUT_SECONDS:-45}

if [[ ! -f "$PACKAGE" ]]; then
	echo "Package not found: $PACKAGE" >&2
	exit 1
fi

# The firmware's plugin scanner uses the versioned filename to distinguish
# copies found on the developer drive and SD card. Build output is historically
# named SpaceTime.mmplugin, so derive the proper name from its manifest.
package_name=$(basename "$PACKAGE")
if [[ "$package_name" != *-v*.mmplugin ]]; then
	manifest_version=$(tar -xOf "$PACKAGE" SpaceTime/plugin.json 2>/dev/null |
		awk -F'"' '/"version"[[:space:]]*:/ {print $4; exit}')
	manifest_slug=$(tar -xOf "$PACKAGE" SpaceTime/plugin.json 2>/dev/null |
		awk -F'"' '/"slug"[[:space:]]*:/ {print $4; exit}')
	if [[ -z "$manifest_slug" || -z "$manifest_version" ]]; then
		echo "Could not read plugin slug/version from $PACKAGE" >&2
		exit 1
	fi
	package_name="${manifest_slug}-v${manifest_version}.mmplugin"
fi
if [[ ! -d "$VOLUME" ]]; then
	echo "MetaModule developer volume is not mounted: $VOLUME" >&2
	exit 1
fi

if [[ -z "$DEVICE" ]]; then
	shopt -s nullglob
	devices=(/dev/cu.usbmodem*)
	shopt -u nullglob
	if [[ ${#devices[@]} -ne 1 ]]; then
		echo "Expected one /dev/cu.usbmodem* device; found ${#devices[@]}." >&2
		echo "Pass the device explicitly as the third argument." >&2
		exit 1
	fi
	DEVICE=${devices[0]}
fi
if [[ ! -c "$DEVICE" ]]; then
	echo "Serial device not found: $DEVICE" >&2
	exit 1
fi

identifier=$(diskutil info "$VOLUME" | awk -F: '/Device Identifier/ {gsub(/[[:space:]]/, "", $2); print $2; exit}')
if [[ -z "$identifier" ]]; then
	echo "Could not determine the disk identifier for $VOLUME" >&2
	exit 1
fi

echo "Copying $package_name to $VOLUME"
cp "$PACKAGE" "$VOLUME/$package_name"
sync
diskutil unmount "$VOLUME" >/dev/null
printf 'INSTALL\n' > "$DEVICE"

echo "Install requested; waiting for DEV_MM to return..."
deadline=$((SECONDS + TIMEOUT))
while (( SECONDS < deadline )); do
	if [[ -d "$VOLUME" ]]; then
		echo "MetaModule developer volume is mounted again."
		exit 0
	fi
	if diskutil info "$identifier" >/dev/null 2>&1; then
		diskutil mount "$identifier" >/dev/null 2>&1 || true
	fi
	sleep 1
done

echo "Install was requested, but $VOLUME did not return within ${TIMEOUT}s." >&2
exit 1
