#!/bin/bash
set -Eeuo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
[[ $(uname -s) == Darwin ]] || { echo 'Создание DMG выполняется на Mac.'; exit 2; }
export PYTHONDONTWRITEBYTECODE=1
export PATH="/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin:${PATH:-}"
VERSION=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' "$ROOT/RELEASE_STATUS.json")
python3 "$ROOT/build-dmg.py" --kit "$ROOT" --output "$HOME/Desktop/SH2-macOS-$VERSION-installer"
