#!/bin/bash
# Builds the host-platform package with Premium gateway keys baked in.
# Usage: deploy/build_cli_package.sh [--suffix +cli.1] [build.sh options]

set -o errexit -o nounset -o pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CLI_ENV="$PROJECT_DIR/deploy/.cli-env"
BUILD_DIR="$PROJECT_DIR/deploy/build"

SUFFIX=""
build_args=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --suffix) SUFFIX="$2"; shift 2 ;;
        *) build_args+=("$1"); shift ;;
    esac
done

if [[ ! -f "$CLI_ENV" ]]; then
    echo "FATAL: $CLI_ENV not found. Copy deploy/.cli-env.example there and fill it in." >&2
    exit 1
fi
# shellcheck disable=SC1090
source "$CLI_ENV"

: "${PROD_AGW_PUBLIC_KEY:?PROD_AGW_PUBLIC_KEY is empty — refusing a keyless build}"
: "${DEV_AGW_PUBLIC_KEY:?DEV_AGW_PUBLIC_KEY is empty}"
: "${PROD_S3_ENDPOINT:?PROD_S3_ENDPOINT is empty}"
: "${FALLBACK_S3_ENDPOINT:?FALLBACK_S3_ENDPOINT is empty}"
export PROD_AGW_PUBLIC_KEY DEV_AGW_PUBLIC_KEY PROD_S3_ENDPOINT FALLBACK_S3_ENDPOINT
export DEV_AGW_ENDPOINT DEV_S3_ENDPOINT

"$PROJECT_DIR/deploy/build.sh" --installer all "${build_args[@]}"

CLIENT_BIN="$BUILD_DIR/client/AmneziaVPN"
if [[ ! -x "$CLIENT_BIN" ]]; then
    echo "FATAL: client binary missing: $CLIENT_BIN" >&2
    exit 1
fi
if [[ "$(strings -a "$CLIENT_BIN" | grep -c 'BEGIN PUBLIC KEY')" -lt 2 ]]; then
    echo "FATAL: gateway keys missing from $CLIENT_BIN. Check deploy/.cli-env." >&2
    exit 1
fi
if strings -a "$CLIENT_BIN" | grep -q 'TEMP-DEBUG'; then
    echo "FATAL: TEMP-DEBUG markers present in $CLIENT_BIN." >&2
    exit 1
fi

PKG="$(find "$BUILD_DIR" -maxdepth 1 -name 'AmneziaVPN_*.run' -newer "$CLIENT_BIN" | head -1)"
if [[ -z "$PKG" ]]; then
    echo "FATAL: no installer produced in $BUILD_DIR" >&2
    exit 1
fi

if [[ -n "$SUFFIX" ]]; then
    NEW="${PKG%.run}${SUFFIX}.run"
    mv "$PKG" "$NEW"
    PKG="$NEW"
fi

echo "OK: $PKG"
