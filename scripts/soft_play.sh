#!/usr/bin/env bash
# Soft play child for parkour_play.
# stdin: INPUT lines from C. stdout: SNAP blocks only (line-buffered).
#
# Skip the image ENTRYPOINT (chown -R /home/dev) — it can eat the first-SNAP
# wait. Soft binary is the mounted tip aura only (Linux ELF; Mac host may
# still ship a Linux aarch64 build under ../aura-grok/build/aura).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMG="ghcr.io/cybrid-systems/dev:v1.0.9"
AURA_IN="/workspace/aura-grok/build/aura"

resolve_aura_src() {
  if [[ -n "${AURA_SRC:-}" ]]; then
    printf '%s\n' "$AURA_SRC"
    return 0
  fi
  local c
  for c in \
    "${ROOT}/../aura-grok" \
    "/workspace/aura-grok" \
    "${HOME}/code/claw-space/grok-dev/aura-grok" \
    "${HOME}/code/grok-dev/aura-grok" \
    "/home/dev/code/grok-dev/aura-grok"; do
    if [[ -x "${c}/build/aura" ]]; then
      printf '%s\n' "$(cd "$c" && pwd)"
      return 0
    fi
  done
  echo "soft_play: Soft binary not found. Set AURA_SRC to your aura-grok tree" >&2
  echo "  (needs \$AURA_SRC/build/aura as a Linux ELF). Tried sibling ../aura-grok." >&2
  return 1
}

AURA_SRC="$(resolve_aura_src)"
if [[ ! -x "${AURA_SRC}/build/aura" ]]; then
  echo "soft_play: missing executable ${AURA_SRC}/build/aura" >&2
  exit 1
fi
echo "soft_play: AURA_SRC=${AURA_SRC}" >&2

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
