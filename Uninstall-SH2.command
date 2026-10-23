#!/bin/bash
set -Eeuo pipefail
export PYTHONDONTWRITEBYTECODE=1
ROOT=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
[[ $(uname -s) == Darwin ]] || { echo 'Удаление выполняется на Mac.'; exit 2; }
echo 'Удаление Silent Hill 2 — brkovv. Исходная PC-папка игры не затрагивается.'
/usr/bin/python3 "$ROOT/manage-install.py" uninstall "$@"
if [[ -t 0 && ${SH2_NO_PAUSE:-0} != 1 ]]; then read -r -p 'Нажмите Enter, чтобы закрыть окно…' _ || true; fi
