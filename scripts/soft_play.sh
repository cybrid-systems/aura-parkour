#!/usr/bin/env bash
# Soft play child for parkour_play.
# stdin: INPUT lines from C. stdout: SNAP blocks only (line-buffered).
#
# The dev image ENTRYPOINT chowns -R /home/dev before the command (~40s here,
# longer on a cold disk). parkour_play only waits 120s for the first SNAP, so
# that chown produced "Soft did not produce an initial SNAP" even though
# play.aura emits SNAP v1 ... END before it reads INPUT. Skip the entrypoint
# and drop to dev with gosu. Soft binary is the mounted tip aura only.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
AURA_SRC="${AURA_SRC:-/workspace/aura-grok}"
IMG="ghcr.io/cybrid-systems/dev:v1.0.9"
AURA_IN="/workspace/aura-grok/build/aura"
if docker info >/dev/null 2>&1; then
  DOCKER=(docker)
elif sudo docker info >/dev/null 2>&1; then
  DOCKER=(sudo docker)
else
  echo "soft_play: docker not available" >&2
  exit 1
fi

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

EXTRA=()
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
