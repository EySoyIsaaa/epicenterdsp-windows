#!/usr/bin/env bash
# Process a song through the lab and produce 3 mp3s to A/B on the computer.
#
#   ./process.sh <song.mp3|wav|flac|m4a> [key=value ...]
#
# Examples:
#   ./process.sh "mysong.mp3"
#   ./process.sh "mysong.mp3" depth=0.8 drive=3.0 mix=0.85 split=140
#   ./process.sh "mysong.mp3" intensity=70 sweep=50
#
set -e
cd "$(dirname "$0")"

IN="$1"; shift || true
if [ -z "$IN" ]; then
  echo "usage: ./process.sh <song.mp3|wav|flac|m4a> [key=value ...]"
  exit 1
fi
if [ ! -f "./epicenter_lab.exe" ]; then
  echo "epicenter_lab.exe missing — run ./build.sh first"
  exit 1
fi

base="$(basename "${IN%.*}")"
tmp="_in_${base}.wav"

ffmpeg -y -i "$IN" -ac 2 -ar 44100 -c:a pcm_s16le "$tmp" 2>/dev/null
./epicenter_lab.exe "$tmp" "$base" "$@"

for v in epicenter enhanced glue; do
  ffmpeg -y -i "${base}_${v}.wav" -b:a 320k "${base}_${v}.mp3" 2>/dev/null
  rm -f "${base}_${v}.wav"
done
rm -f "$tmp"

echo ""
echo "Listen and compare:"
echo "  ${base}_epicenter.mp3   (current app sound = baseline)"
echo "  ${base}_enhanced.mp3    (baseline + new BassGlue: saturation + compression)"
echo "  ${base}_glue.mp3        (only the new stage, no Epicenter)"
