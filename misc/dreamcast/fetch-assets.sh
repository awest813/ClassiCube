#!/bin/bash
# Download disc image assets for make dreamcast (texture + audio packs).
# IP.BIN must still be generated separately — see readme.txt
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

fetch "https://www.classicube.net/static/default.zip" "classicube.zip"
fetch "https://www.classicube.net/static/audio.zip"   "audio.zip"

echo "Dreamcast assets ready in $(pwd)"
