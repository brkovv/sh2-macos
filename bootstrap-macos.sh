#!/bin/bash
# Automatic source installer. Apple/vendor authentication remains interactive.
# macOS ships Bash 3.2: nounset treats empty arrays as unbound there.
set -Eeo pipefail
export PYTHONDONTWRITEBYTECODE=1
export PATH="/opt/homebrew/bin:/opt/homebrew/sbin:/usr/bin:/bin:/usr/sbin:/sbin:${PATH:-}"
export HOMEBREW_NO_AUTO_UPDATE=1
PACKAGE=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
[[ $(uname -s) == Darwin && $(uname -m) == arm64 ]] || { echo 'Нужен Apple Silicon Mac без Rosetta.'; exit 2; }
OS_VERSION=$(sw_vers -productVersion)
[[ ${OS_VERSION%%.*} -ge 15 ]] || { echo 'Нужна macOS 15 или новее.'; exit 2; }
BOOTSTRAP_LOG=$(mktemp "${TMPDIR:-/tmp}/sh2-bootstrap.XXXXXX")
BASE=''
finish() {
    code=$?
    if [[ -n "$BASE" && -d "$BASE/logs" ]]; then
        cp "$BOOTSTRAP_LOG" "$BASE/logs/bootstrap-$(date +%Y%m%d-%H%M%S)-$$.log" || true
    fi
    if [[ $code -ne 0 ]]; then
        printf '\nУстановка остановилась (код %s). Повторный запуск продолжит доступные шаги.\nЛог: %s\n' "$code" "$BOOTSTRAP_LOG"
        if [[ -t 0 && ${SH2_NO_PAUSE:-0} != 1 ]]; then read -r -p 'Нажмите Enter, чтобы закрыть окно…' _ || true; fi
    fi
}
trap finish EXIT
exec > >(tee -a "$BOOTSTRAP_LOG") 2>&1
printf 'Silent Hill 2 — brkovv %s\n' "$(cat "$PACKAGE/VERSION")"
echo 'Понадобится своя PC/Enhanced Edition копия игры. Системные окна могут запросить пароль.'
echo '1/5 Проверяем комплект и инструменты Apple…'
(cd "$PACKAGE" && /usr/bin/shasum -a 256 -c FILES.sha256 >/dev/null)
if ! xcrun --sdk macosx --show-sdk-path >/dev/null 2>&1; then
    xcode-select --install || true
    echo 'Завершите установку Command Line Tools в окне Apple. Ждём до 20 минут; Ctrl+C — отмена.'
    attempts=0
    until xcrun --sdk macosx --show-sdk-path >/dev/null 2>&1; do
        attempts=$((attempts+1))
        [[ $attempts -le 120 ]] || { echo 'Ожидание завершено. После установки инструментов откройте Install-SH2.command снова.'; exit 2; }
        sleep 10
    done
fi
requested=(); game_args=(); runtime_args=(); has_game=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --base) [[ $# -ge 2 ]] || { echo 'После --base нужен путь.'; exit 2; }; requested=(--base "$2"); shift 2 ;;
        --game) [[ $# -ge 2 ]] || { echo 'После --game нужен путь.'; exit 2; }; game_args=(--game "$2"); has_game=1; shift 2 ;;
        *) runtime_args+=("$1"); shift ;;
    esac
done
INFO=$(/usr/bin/python3 "$PACKAGE/manage-install.py" install "${requested[@]}")
BASE=$(printf '%s' "$INFO" | /usr/bin/python3 -c 'import json,sys; print(json.load(sys.stdin)["base"])')
PACKAGE=$(printf '%s' "$INFO" | /usr/bin/python3 -c 'import json,sys; print(json.load(sys.stdin)["kit"])')
mkdir -p "$BASE/logs" "$BASE/setup"
printf 'Приложение: %s/Applications/Silent Hill 2.app\nДанные: %s\n' "$HOME" "$BASE"
echo '2/5 Устанавливаем недостающие зависимости…'
if ! command -v brew >/dev/null 2>&1; then
    echo 'Homebrew не найден. Запускаем официальный установщик; прочитайте его запросы.'
    BREW_INSTALL="$BASE/setup/homebrew-install.sh"
    [[ ! -L "$BREW_INSTALL" ]] || { echo 'Ссылка вместо установщика Homebrew.'; exit 2; }
    /usr/bin/curl --fail --location --retry 2 --connect-timeout 15 --max-time 300 \
        'https://raw.githubusercontent.com/Homebrew/install/85d1a3fc80d8a63932f632aca8f787de11d14a64/install.sh' -o "$BREW_INSTALL"
    BREW_SHA=$(/usr/bin/shasum -a 256 "$BREW_INSTALL")
    [[ ${BREW_SHA%% *} == 5f333bbe53bc490e51e7ccb1df8779b3dd6ee73a1a7379efda216edb08ccb148 ]] || { echo 'SHA-256 установщика Homebrew не совпала.'; exit 2; }
    /bin/bash "$BREW_INSTALL"
fi
missing=()
for formula in cmake ninja pkgconf python@3.12 sdl2 ffmpeg; do
    if ! brew list --versions "$formula" >/dev/null 2>&1; then missing+=("$formula"); fi
done
if [[ ${#missing[@]} -gt 0 ]]; then brew install "${missing[@]}"; fi
PY="$(brew --prefix python@3.12)/bin/python3.12"
[[ ! -L "$BASE/venv" ]] || { echo 'Ссылка вместо среды Python; установка остановлена.'; exit 2; }
if [[ ! -x "$BASE/venv/bin/python3" ]]; then "$PY" -m venv "$BASE/venv"; fi
PY="$BASE/venv/bin/python3"
if ! "$PY" -c 'import capstone; assert capstone.__version__=="5.0.7"' >/dev/null 2>&1; then
    "$PY" -m pip install --disable-pip-version-check 'capstone==5.0.7'
fi
export PATH="$BASE/venv/bin:$PATH"
echo '3/5 Подготавливаем вашу копию игры…'
if [[ ! -d "$BASE/game" && $has_game == 0 ]]; then
    LEGACY="$HOME/Library/Application Support/SH2-Recomp/2b7a75bcb3a601b9/game"
    if [[ -f "$LEGACY/sh2pc.exe" ]]; then GAME="$LEGACY"; else
        GAME=$(osascript -e 'POSIX path of (choose folder with prompt "Выберите свою Silent Hill 2 с Enhanced Edition (sh2pc.exe, data, sh2e)")')
    fi
    game_args=(--game "$GAME")
fi
"$PY" "$PACKAGE/installer.py" --base "$BASE" "${game_args[@]}" "${runtime_args[@]}"
