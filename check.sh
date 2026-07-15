#!/bin/sh
# SpaceTime CI-style check: build plugin, build and run host-side tests.
# Usage: ./check.sh [RACK_DIR]
set -e

cd "$(dirname "$0")"

RACK_DIR="${1:-${RACK_DIR:-$HOME/Development/Rack-SDK}}"

echo "== Host-side unit tests =="
make -C test test

if [ -d "$RACK_DIR" ]; then
	echo "== Plugin build (RACK_DIR=$RACK_DIR) =="
	make RACK_DIR="$RACK_DIR" -j4
else
	echo "!! Rack SDK not found at $RACK_DIR — skipping plugin build" >&2
	exit 1
fi

echo "== OK =="
