#!/usr/bin/env python3
"""Ask DeepSeek V4.1 Flash for a parkour chunk-grammar body.

Soft owns the world. This process only writes a proposed lambda string to the
path given on argv. It does not swap strategies, spawn obstacles, or touch
score. stdout stays empty so a Soft `shell` child cannot corrupt the SNAP pipe.
"""
from __future__ import annotations

import json
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path

DEFAULT_MODEL = "deepseek-flash"  # DeepSeek V4.1 Flash, same id as aura-typeplay
DEFAULT_BASE = "https://api.deepseek.com"

_FORBIDDEN = (
    "set!",
    "*score*",
    "display",
    "shell",
    "http",
    "set-code",
    "mutate:",
    "eval",
    "load ",
    "require",
    "write-file",
    "read-file",
    "command-output",
    "score",
)


def _key_file_candidates() -> list[Path]:
    env = os.environ.get("DEEPSEEK_API_KEY_FILE", "").strip()
    home = Path.home()
    out: list[Path] = []
    if env:
        out.append(Path(env).expanduser())
    out.extend(
        [
            home / "code" / "keys" / "deepseek",
            Path("/home/dev/code/keys/deepseek"),
            home / ".config" / "aura-build" / "deepseek_api_key",
        ]
    )
    return out


def resolve_api_key() -> str:
    direct = os.environ.get("DEEPSEEK_API_KEY", "").strip()
    if direct:
        return direct
    for path in _key_file_candidates():
        try:
            if path.is_file():
                text = path.read_text(encoding="utf-8").strip()
                if text:
                    return text.splitlines()[0].strip()
        except OSError:
            continue
    return ""


def chat_url(base: str) -> str:
    b = base.rstrip("/")
    if b.endswith("/chat/completions"):
        return b
    return b + "/chat/completions"


def _strip_fence(text: str) -> str:
    raw = text.strip()
    if raw.startswith("```"):
        lines = raw.splitlines()
        if lines and lines[0].startswith("```"):
            lines = lines[1:]
        if lines and lines[-1].startswith("```"):
            lines = lines[:-1]
        raw = "\n".join(lines).strip()
    return raw


def balanced(s: str) -> bool:
    depth = 0
    in_str = False
    esc = False
    for ch in s:
        if in_str:
            if esc:
                esc = False
            elif ch == "\\":
                esc = True
            elif ch == '"':
                in_str = False
            continue
        if ch == '"':
            in_str = True
        elif ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth < 0:
                return False
    return depth == 0 and not in_str


def body_locally_ok(body: str) -> bool:
    """Host-side shape check. Soft repeats a stricter gate before swap!."""
    s = body.strip()
    if not s.startswith("(lambda") or "origin" not in s or "list" not in s:
        return False
    if len(s) < 40 or len(s) > 2000:
        return False
    low = s.lower()
    for bad in _FORBIDDEN:
        if bad in s or bad in low:
            return False
    return balanced(s)


def propose_body(key: str) -> str:
    model = (os.environ.get("DEEPSEEK_MODEL") or DEFAULT_MODEL).strip() or DEFAULT_MODEL
    base = (os.environ.get("DEEPSEEK_BASE_URL") or DEFAULT_BASE).strip() or DEFAULT_BASE
    prompt = (
        "Propose ONE Aura/Soft lambda for a parkour chunk grammar. "
        "Output the lambda only, no markdown, no explanation.\n"
        "Signature: (lambda (origin) ...)\n"
        "Call (gap origin) for the gap width (a number).\n"
        "Return (list (+ origin STRIDE) OBS) where STRIDE is an integer from 18 to 22 "
        "and OBS is a list of 5 to 8 obstacle rows.\n"
        "Each row is (list KIND X Y Z W H D FLAGS).\n"
        "KIND is an integer: 0 gap, 1 beam, 2 block, 3 pad, 4 coin.\n"
        "X is (+ origin N) with 2 <= N < STRIDE. Z is -2, 0, or 2. "
        "FLAGS is 0. Kind 2 block: Y 0, H 2, W 2, D 2, on one side. Kind 1 beam: Y 1 (never 0), H 1, Z -2, D 4, so a slide clears it. Kind 4 coin: Y 1 or 3, W 2, D 2 (a width-1 coin on an odd X is missed at speed 2). Kind 0 gap: Y 0, H 1, D 1, Z -2, W is the gap width. Include one beam, one block, and one coin.\n"
        "Use the gap width only as W of a kind-0 row.\n"
        "Do not mention score, alive, tick, set!, display, shell, http, eval, or mutate.\n"
        "Example shape (do not copy numbers verbatim):\n"
        "(lambda (origin) (let ((gw (gap origin))) "
        "(list (+ origin 22) (list (list 2 (+ origin 5) 0 -1 2 2 2 0) "
        "(list 4 (+ origin 10) 1 0 2 1 2 0) "
        "(list 0 (+ origin 16) 0 -2 gw 1 4 0)))))"
    )
    body = {
        "model": model,
        "messages": [
            {
                "role": "system",
                "content": (
                    "You write a single Aura lambda for obstacle geometry. "
                    "JSON is not required. No score rules."
                ),
            },
            {"role": "user", "content": prompt},
        ],
        "temperature": 0.4,
        "thinking": {"type": "disabled"},
    }
    req = urllib.request.Request(
        chat_url(base),
        data=json.dumps(body).encode("utf-8"),
        headers={
            "Content-Type": "application/json",
            "Authorization": f"Bearer {key}",
            "Accept": "application/json",
        },
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=25) as resp:
            raw = json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        print(f"propose_objects: http {exc.code}", file=sys.stderr)
        return ""
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError, OSError) as exc:
        print(f"propose_objects: {type(exc).__name__}", file=sys.stderr)
        return ""
    try:
        content = raw["choices"][0]["message"].get("content") or ""
    except (KeyError, IndexError, TypeError):
        print("propose_objects: shape", file=sys.stderr)
        return ""
    return _strip_fence(content)


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print("usage: propose_objects.py OUT_PATH", file=sys.stderr)
        return 2
    key = resolve_api_key()
    if not key:
        print("propose_objects: no DeepSeek key", file=sys.stderr)
        return 1
    body = propose_body(key)
    if not body_locally_ok(body):
        print("propose_objects: rejected proposal", file=sys.stderr)
        return 1
    path = Path(argv[1])
    path.write_text(body.strip() + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
