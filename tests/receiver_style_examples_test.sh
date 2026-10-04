#!/usr/bin/env bash
set -euo pipefail

SOURCE_DIR="${1:?usage: receiver_style_examples_test.sh <source-dir>}"

python3 - "${SOURCE_DIR}" <<'PY'
import pathlib
import re
import sys

source_dir = pathlib.Path(sys.argv[1])
paths = [
    source_dir / "docs" / "audio-sus-facade-spec.md",
    source_dir / "examples" / "audio-sus-c89" / "main.c",
    source_dir / "examples" / "audio-vox-intro-c89" / "main.c",
    source_dir / "examples" / "audio-live-vox-c89" / "main.c",
    source_dir / "examples" / "sus-vox-intro-c89" / "main.c",
    source_dir / "examples" / "sus-live-vox-c89" / "main.c",
]

patterns = [
    (
        re.compile(r"\bmodel->open_cached\s*\("),
        "cached speech instance opening is a constructor; use cpkt_sus_open_cached",
    ),
    (
        re.compile(r"\bcpkt_audio_decoder_(?:read_f32_mono_16k|info|destroy)\s*\("),
        "decoder handle operations in docs/examples must use decoder->method",
    ),
    (
        re.compile(r"\bcpkt_audio_encoder_(?:write_f32|close|destroy)\s*\("),
        "encoder handle operations in docs/examples must use encoder->method",
    ),
    (
        re.compile(r"\bcpkt_audio_vox_(?:push_f32_mono_16k|flush|destroy)\s*\("),
        "VOX handle operations in docs/examples must use vox->method",
    ),
    (
        re.compile(r"\bcpkt_audio_capture_(?:start|read_f32_mono_16k|stop|destroy)\s*\("),
        "capture handle operations in docs/examples must use capture->method",
    ),
    (
        re.compile(r"\bcpkt_audio_playback_(?:start|write_f32_mono_16k|drain|stop|destroy)\s*\("),
        "playback handle operations in docs/examples must use playback->method",
    ),
    (
        re.compile(r"\bcpkt_sus_model_(?:open_path|open_cached|info|create_transcriber|destroy)\s*\("),
        "docs/examples must use the cpkt_sus instance API",
    ),
    (
        re.compile(r"\bcpkt_sus_model\s+\*"),
        "docs/examples must name the speech facade instance cpkt_sus",
    ),
    (
        re.compile(r"\bcpkt_sus_model_config\b"),
        "docs/examples must use cpkt_sus_config for path-open config",
    ),
    (
        re.compile(
            r"\bcpkt_sus_transcriber_(?:transcribe_f32_mono_16k|"
            r"transcribe_f32_mono_16k_text|transcribe_audio_decoder_segmented|"
            r"transcribe_audio_decoder_segmented_text|revised_text|destroy)\s*\("
        ),
        "transcriber handle operations in docs/examples must use transcriber->method",
    ),
]

failures = []
for path in paths:
    text = path.read_text(encoding="utf-8")
    for pattern, message in patterns:
        for match in pattern.finditer(text):
            line = text.count("\n", 0, match.start()) + 1
            failures.append(f"{path}:{line}: {message}")

if failures:
    for failure in failures:
        print(failure, file=sys.stderr)
    sys.exit(1)
PY
