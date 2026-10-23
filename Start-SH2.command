#!/bin/bash
# Source-only alpha launcher. The user supplies the full PC/EE installation.
set -Eeuo pipefail
PACKAGE=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
[[ $(uname -s) == Darwin && $(uname -m) == arm64 ]] || { echo 'Нужен Apple Silicon Mac (без Rosetta).'; exit 2; }
OS_VERSION=$(sw_vers -productVersion)
[[ ${OS_VERSION%%.*} -ge 15 ]] || { echo 'Этот экспериментальный комплект рассчитан на macOS 15 или новее.'; exit 2; }
export PATH="/opt/homebrew/bin:/opt/homebrew/sbin:/usr/bin:/bin:/usr/sbin:/sbin:${PATH:-}"
export HOMEBREW_NO_AUTO_UPDATE=1
export PYTHONDONTWRITEBYTECODE=1
BASE="$HOME/Library/Application Support/SH2-brkovv"
read -r VERSION < "$PACKAGE/VERSION"
printf 'Silent Hill 2 — brkovv %s\n' "$VERSION"
printf 'Нужна своя копия PC-игры с Enhanced Edition. Первый запуск копирует её и собирает порт.\n'
if ! xcrun --sdk macosx --show-sdk-path >/dev/null 2>&1; then
    xcode-select --install || true
    echo 'Дождитесь установки Apple Command Line Tools и откройте запускатель снова.'
    exit 2
fi
command -v brew >/dev/null || { echo 'Сначала установите Homebrew с https://brew.sh и повторите запуск.'; exit 2; }
missing=()
for formula in cmake ninja pkgconf python@3.12 sdl2 ffmpeg; do
    if ! brew list --versions "$formula" >/dev/null 2>&1; then missing+=("$formula"); fi
done
if [[ ${#missing[@]} -gt 0 ]]; then brew install "${missing[@]}"; fi
PY="$(brew --prefix python@3.12)/bin/python3.12"
mkdir -p "$BASE"
if [[ ! -x "$BASE/venv/bin/python3" ]]; then "$PY" -m venv "$BASE/venv"; fi
PY="$BASE/venv/bin/python3"
if ! "$PY" -c 'import capstone; assert capstone.__version__=="5.0.7"' >/dev/null 2>&1; then
    "$PY" -m pip install --disable-pip-version-check 'capstone==5.0.7'
fi
export PATH="$BASE/venv/bin:$PATH"
args=()
if [[ ! -d "$BASE/game" ]]; then
    GAME=$(osascript -e 'POSIX path of (choose folder with prompt "Выберите свою Silent Hill 2 с Enhanced Edition (sh2pc.exe, data, sh2e)")')
    args+=(--game "$GAME")
fi
"$PY" "$PACKAGE/installer.py" "${args[@]}" "$@"
