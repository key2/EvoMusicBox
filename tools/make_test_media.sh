#!/usr/bin/env bash
# Generates small test media files (wav / mp3 / mp4 / webp) with the ffmpeg CLI for manual testing
# of the import flow. Usage: tools/make_test_media.sh [outdir]   (default: ./test-media)
set -euo pipefail
out="${1:-test-media}"
mkdir -p "$out"
command -v ffmpeg >/dev/null || { echo "ffmpeg CLI not found"; exit 1; }

ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:duration=3" -ac 1 "$out/tone-3s.wav"
ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=220:duration=8" -f lavfi -i "sine=frequency=330:duration=8" \
       -filter_complex "[0:a][1:a]amerge=inputs=2[a]" -map "[a]" -ac 2 -codec:a libmp3lame -q:a 4 "$out/chord-8s.mp3"
ffmpeg -y -loglevel error -f lavfi -i "testsrc=size=320x180:rate=25:duration=6" \
       -f lavfi -i "anoisesrc=color=pink:duration=6:amplitude=0.3" -shortest -c:v libx264 -pix_fmt yuv420p -c:a aac "$out/video-6s.mp4"
ffmpeg -y -loglevel error -f lavfi -i "color=c=orange:s=128x128:d=1" -frames:v 1 -c:v libwebp "$out/icon.webp" 2>/dev/null || true
ls -la "$out"
