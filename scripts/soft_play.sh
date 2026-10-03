#!/usr/bin/env bash
# Soft play child for parkour_play.
# stdin: INPUT lines from C. stdout: SNAP blocks only (line-buffered).
#
# Modes:
# 1) Host with Docker: run Soft in ghcr.io/cybrid-systems/dev:v1.0.9 (skip ENTRYPOINT).
# 2) Already inside that Linux image (no docker): run Soft natively, with
#    /workspace/{aura-grok,aura-parkour} symlinks so Soft loads stay fixed.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMG="ghcr.io/cybrid-systems/dev:v1.0.9"
AURA_IN="/workspace/aura-grok/build/aura"

resolve_aura_src() {
  if [[ -n "${AURA_SRC:-}" && -x "${AURA_SRC}/build/aura" ]]; then
    printf '%s\n' "$(cd "$AURA_SRC" && pwd)"
    return 0
  fi
  if [[ -n "${AURA_SRC:-}" ]]; then
    echo "soft_play: AURA_SRC=${AURA_SRC} but missing ${AURA_SRC}/build/aura" >&2
  fi
  local c
  for c in \
    "${ROOT}/../aura-grok" \
    "/workspace/aura-grok" \
    "/home/dev/code/grok-dev/aura-grok" \
    "${HOME}/code/claw-space/grok-dev/aura-grok" \
    "${HOME}/code/grok-dev/aura-grok"; do
    if [[ -x "${c}/build/aura" ]]; then
      printf '%s\n' "$(cd "$c" && pwd)"
      return 0
    fi
  done
  echo "soft_play: Soft binary not found. Set AURA_SRC to your aura-grok tree" >&2
  return 1
}

AURA_SRC="$(resolve_aura_src)"
echo "soft_play: AURA_SRC=${AURA_SRC}" >&2

resolve_key_file() {
  if [[ -n "${DEEPSEEK_API_KEY_FILE:-}" && -f "${DEEPSEEK_API_KEY_FILE}" ]]; then
    printf '%s\n' "${DEEPSEEK_API_KEY_FILE}"
    return 0
  fi
  local c
  for c in "${HOME}/code/keys/deepseek" "/home/dev/code/keys/deepseek" \
           "${HOME}/.config/aura-build/deepseek_api_key"; do
    if [[ -f "$c" ]]; then
      printf '%s\n' "$c"
      return 0
    fi
  done
  return 1
}

have_docker() {
  if docker info >/dev/null 2>&1; then return 0; fi
  if sudo docker info >/dev/null 2>&1; then return 0; fi
  return 1
}

run_native() {
  # Soft scripts hardcode /workspace/aura-parkour and /workspace/aura-grok.
  mkdir -p /workspace
  ln -sfn "$ROOT" /workspace/aura-parkour
  ln -sfn "$AURA_SRC" /workspace/aura-grok
  export AURA_PATH=/workspace/aura-grok/lib
  export AURA_PIPELINE_STRICT=0
  export AURA_SANDBOX=off
  export AURA_BIN=/workspace/aura-grok/build/aura
  if KEY_FILE="$(resolve_key_file)"; then
    export DEEPSEEK_API_KEY_FILE="$KEY_FILE"
  fi
  export DEEPSEEK_MODEL="${DEEPSEEK_MODEL:-deepseek-flash}"
  export DEEPSEEK_BASE_URL="${DEEPSEEK_BASE_URL:-https://api.deepseek.com}"
  if [[ -n "${PARKOUR_PROPOSE:-}" ]]; then
    export PARKOUR_PROPOSE
  fi
  echo "soft_play: native Soft (no docker)" >&2
  exec /usr/bin/stdbuf -oL -eL "$AURA_BIN" /workspace/aura-parkour/soft/parkour/play.aura
}

run_docker() {
  local DOCKER
  if docker info >/dev/null 2>&1; then DOCKER=(docker)
  else DOCKER=(sudo docker)
  fi
  local EXTRA=()
  if KEY_FILE="$(resolve_key_file)"; then
    EXTRA+=(-v "${KEY_FILE}:/run/secrets/deepseek:ro" -e DEEPSEEK_API_KEY_FILE=/run/secrets/deepseek)
  fi
  if [[ -n "${DEEPSEEK_API_KEY:-}" ]]; then
    EXTRA+=(-e DEEPSEEK_API_KEY)
  fi
  if [[ -n "${PARKOUR_PROPOSE:-}" ]]; then
    EXTRA+=(-e PARKOUR_PROPOSE)
  fi
  EXTRA+=(-e "DEEPSEEK_MODEL=${DEEPSEEK_MODEL:-deepseek-flash}")
  EXTRA+=(-e "DEEPSEEK_BASE_URL=${DEEPSEEK_BASE_URL:-https://api.deepseek.com}")
  echo "soft_play: docker Soft via ${IMG}" >&2
  exec "${DOCKER[@]}" run --rm -i --entrypoint /usr/local/bin/gosu \
    -v "${AURA_SRC}:/workspace/aura-grok" \
    -v "${ROOT}:/workspace/aura-parkour" \
    -w /workspace/aura-parkour \
    -e AURA_PATH=/workspace/aura-grok/lib \
    -e AURA_PIPELINE_STRICT=0 \
    -e AURA_SANDBOX=off \
    -e "AURA_BIN=${AURA_IN}" \
    "${EXTRA[@]}" \
    "${IMG}" \
    dev /usr/bin/stdbuf -oL -eL "${AURA_IN}" /workspace/aura-parkour/soft/parkour/play.aura
}

if have_docker; then
  run_docker
else
  run_native
fi
