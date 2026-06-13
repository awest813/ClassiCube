#!/bin/bash
# Download disc image assets for make dreamcast (texture + audio packs).
# Also attempts to generate IP.BIN when makeip is available.
set -euo pipefail
cd "$(dirname "$0")"

fetch() {
	local url="$1" dest="$2"
	if [ -f "$dest" ]; then
		echo "Already present: $dest"
		return
	fi
	echo "Downloading $dest ..."
	wget -q -O "$dest" "$url"
}

ensure_ipbin() {
	if [ -f IP.BIN ]; then
		echo "Already present: IP.BIN"
		return
	fi
	if command -v makeip >/dev/null 2>&1; then
		echo "Generating IP.BIN via makeip ..."
		makeip ip.txt IP.BIN
		return
	fi
	echo "Note: IP.BIN missing — install makeip or copy a prebuilt IP.BIN here (see readme.txt)"
}

fetch "https://www.classicube.net/static/default.zip" "classicube.zip"
fetch "https://www.classicube.net/static/audio.zip"   "audio.zip"
ensure_ipbin

echo "Dreamcast assets ready in $(pwd)"
