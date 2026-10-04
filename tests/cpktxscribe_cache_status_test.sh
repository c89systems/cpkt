#!/usr/bin/env sh
set -eu

if [ "$#" -lt 1 ]; then
  printf 'usage: cpktxscribe_cache_status_test.sh [runner ...] <cpktxscribe>\n' >&2
  exit 2
fi

work_root=${TMPDIR:-/tmp}/cpktxscribe-cache-status-test.$$
audio_path=$work_root/input.wav
cache_dir=$work_root/cache
stdout_path=$work_root/stdout.txt
stderr_path=$work_root/stderr.txt

cleanup() {
  rm -rf "$work_root"
}
trap cleanup EXIT HUP INT TERM

mkdir -p "$cache_dir"

python3 - "$audio_path" <<'PY'
import math
import struct
import sys
import wave

path = sys.argv[1]
with wave.open(path, "wb") as wav:
    wav.setnchannels(1)
    wav.setsampwidth(2)
    wav.setframerate(16000)
    frames = bytearray()
    for i in range(160):
        sample = int(1000 * math.sin(2.0 * math.pi * 440.0 * i / 16000.0))
        frames.extend(struct.pack("<h", sample))
    wav.writeframes(bytes(frames))
PY

set +e
"$@" --offline --cache-dir "$cache_dir" --model tiny "$audio_path" \
  >"$stdout_path" 2>"$stderr_path"
rc=$?
set -e

if [ "$rc" -eq 0 ]; then
  printf 'cpktxscribe unexpectedly succeeded without a cached offline model\n' >&2
  printf 'stdout: %s\nstderr: %s\n' "$stdout_path" "$stderr_path" >&2
  exit 1
fi

if [ -s "$stdout_path" ]; then
  printf 'cpktxscribe emitted transcript text despite model-open failure\n' >&2
  printf 'stdout: %s\nstderr: %s\n' "$stdout_path" "$stderr_path" >&2
  exit 1
fi

if ! grep -F -- "status source=$audio_path model=tiny cache=$cache_dir/ggml-tiny.bin cache_state=missing-offline" "$stderr_path" >/dev/null 2>&1; then
  printf 'cpktxscribe did not print the initial cache status line\n' >&2
  cat "$stderr_path" >&2
  exit 1
fi

if ! grep -F -- "status model_cache=lookup model=tiny cache=$cache_dir/ggml-tiny.bin" "$stderr_path" >/dev/null 2>&1; then
  printf 'cpktxscribe did not print the cache lookup phase\n' >&2
  cat "$stderr_path" >&2
  exit 1
fi

if ! grep -F -- "status model_cache=missing model=tiny cache=$cache_dir/ggml-tiny.bin" "$stderr_path" >/dev/null 2>&1; then
  printf 'cpktxscribe did not print the cache miss phase\n' >&2
  cat "$stderr_path" >&2
  exit 1
fi

if ! grep -F -- 'model open failed: I/O error' "$stderr_path" >/dev/null 2>&1; then
  printf 'cpktxscribe did not report the offline missing-cache failure\n' >&2
  cat "$stderr_path" >&2
  exit 1
fi

if grep -F -- 'whisper_' "$stderr_path" >/dev/null 2>&1; then
  printf 'cpktxscribe leaked whisper backend logs in default mode\n' >&2
  cat "$stderr_path" >&2
  exit 1
fi
