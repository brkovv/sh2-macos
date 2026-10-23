#!/bin/bash
set -Eeuo pipefail
export PYTHONDONTWRITEBYTECODE=1
ROOT=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
[[ $(uname -s) == Darwin ]] || { echo 'Обновление выполняется на Mac.'; exit 2; }
echo 'Проверяем обновления brkovv/sh2-macos. Закройте игру перед обновлением.'
INFO=$(/usr/bin/python3 "$ROOT/manage-install.py" update)
printf '%s\n' "$INFO"
UPDATED=$(printf '%s' "$INFO" | /usr/bin/python3 -c 'import json,sys; print(int(json.load(sys.stdin)["updated"]))')
if [[ $UPDATED == 1 ]]; then
    KIT=$(printf '%s' "$INFO" | /usr/bin/python3 -c 'import json,sys; print(json.load(sys.stdin)["kit"])')
    exec /bin/bash "$KIT/Start-SH2.command" "$@"
fi
