#!/bin/bash
set -Eeuo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
exec /bin/bash "$ROOT/bootstrap-macos.sh" "$@"
