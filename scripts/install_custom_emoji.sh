#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST_DIR="$PROJECT_DIR/main/boards/custom/freenove-s3-2.8-lcd/emoji"
TARGET_SIZE=200
PORT="${ESP_PORT:-/dev/cu.usbmodem5C630518811}"

EMOTION_LIST="neutral happy laughing funny sad angry crying loving embarrassed surprised shocked thinking winking cool relaxed delicious kissy confident sleepy silly confused"

usage() {
  cat <<'EOF'
Usage:
  ./scripts/install_custom_emoji.sh SOURCE [--flash] [--port PORT]

SOURCE is either a directory of PNGs or a single PNG. Each file must be
named after the emotion it replaces (for example happy.png) and must be
square with a transparent background.

Emotions:
  neutral happy laughing funny sad angry crying loving embarrassed
  surprised shocked thinking winking cool relaxed delicious kissy
  confident sleepy silly confused

What it does:
  1. Validates each PNG is square and has an alpha channel.
  2. Resizes it to 200x200 (no crop, no distortion) when needed.
  3. Copies it to main/boards/custom/freenove-s3-2.8-lcd/emoji/<emotion>.png.
  4. With --flash, builds, flashes and opens the monitor.

Examples:
  ./scripts/install_custom_emoji.sh ~/art/emotions
  ESP_PORT=/dev/cu.usbmodemXXXX ./scripts/install_custom_emoji.sh happy.png --flash
EOF
}

is_known_emotion() {
  case " $EMOTION_LIST " in
    *" $1 "*) return 0 ;;
    *) return 1 ;;
  esac
}

FLASH=false
SOURCE=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --flash) FLASH=true ;;
    --port)
      [[ $# -ge 2 ]] || { echo "Error: --port needs a value" >&2; exit 1; }
      PORT="$2"
      shift
      ;;
    -h|--help) usage; exit 0 ;;
    -*)
      echo "Error: unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
    *)
      [[ -z "$SOURCE" ]] || { echo "Error: only one SOURCE allowed" >&2; exit 1; }
      SOURCE="$1"
      ;;
  esac
  shift
done

if [[ -z "$SOURCE" ]]; then
  usage >&2
  exit 1
fi

[[ -d "$PROJECT_DIR" ]] || { echo "Error: project dir not found: $PROJECT_DIR" >&2; exit 1; }
command -v sips >/dev/null || { echo "Error: sips not found (macOS required)" >&2; exit 1; }

if [[ -d "$SOURCE" ]]; then
  shopt -s nullglob
  FILES=("$SOURCE"/*.png "$SOURCE"/*.PNG)
  shopt -u nullglob
elif [[ -f "$SOURCE" ]]; then
  case "$SOURCE" in
    *.png|*.PNG) ;;
    *) echo "Error: use PNG so transparency is preserved." >&2; exit 1 ;;
  esac
  FILES=("$SOURCE")
else
  echo "Error: source not found: $SOURCE" >&2
  exit 1
fi

if [[ ${#FILES[@]} -eq 0 ]]; then
  echo "Error: no PNG files found in $SOURCE" >&2
  exit 1
fi

mkdir -p "$DEST_DIR"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

INSTALLED=()
SKIPPED=()
INVALID=()

for FILE in "${FILES[@]}"; do
  NAME="$(basename "$FILE")"
  NAME="${NAME%.*}"
  NAME="${NAME//[^A-Za-z0-9._-]/_}"

  if ! is_known_emotion "$NAME"; then
    echo "Skipping unknown emotion: $NAME"
    SKIPPED+=("$NAME")
    continue
  fi

  INFO="$(sips -g pixelWidth -g pixelHeight -g hasAlpha "$FILE")"
  WIDTH="$(awk '/pixelWidth/{print $2}' <<<"$INFO")"
  HEIGHT="$(awk '/pixelHeight/{print $2}' <<<"$INFO")"
  HAS_ALPHA="$(awk '/hasAlpha/{print $2}' <<<"$INFO")"

  if [[ "$WIDTH" != "$HEIGHT" ]]; then
    echo "Error: $NAME.png is ${WIDTH}x${HEIGHT}; a square source is required." >&2
    INVALID+=("$NAME")
    continue
  fi

  if [[ "$HAS_ALPHA" != "yes" ]]; then
    echo "Warning: $NAME.png has no alpha channel; it will render with an opaque background." >&2
  fi

  if [[ "$WIDTH" != "$TARGET_SIZE" ]]; then
    OUT="$WORK_DIR/$NAME.png"
    sips --resampleWidth "$TARGET_SIZE" "$FILE" --out "$OUT" >/dev/null
  else
    OUT="$FILE"
  fi

  CHECK="$(sips -g pixelWidth -g pixelHeight "$OUT")"
  OUT_W="$(awk '/pixelWidth/{print $2}' <<<"$CHECK")"
  OUT_H="$(awk '/pixelHeight/{print $2}' <<<"$CHECK")"
  if [[ "$OUT_W" != "$TARGET_SIZE" || "$OUT_H" != "$TARGET_SIZE" ]]; then
    echo "Error: failed to produce ${TARGET_SIZE}x${TARGET_SIZE} for $NAME." >&2
    INVALID+=("$NAME")
    continue
  fi

  if [[ "$OUT" != "$DEST_DIR/$NAME.png" ]]; then
    cp "$OUT" "$DEST_DIR/$NAME.png"
  fi
  INSTALLED+=("$NAME")
done

echo
echo "Installed to $DEST_DIR:"
for NAME in "${INSTALLED[@]}"; do
  echo "  - $NAME.png"
done
[[ ${#INSTALLED[@]} -gt 0 ]] || echo "  (none)"

MISSING=()
for EMOTION in $EMOTION_LIST; do
  [[ -f "$DEST_DIR/$EMOTION.png" ]] || MISSING+=("$EMOTION")
done
if [[ ${#MISSING[@]} -gt 0 ]]; then
  echo
  echo "Not installed (will fall back to the built-in glyph):"
  echo "  ${MISSING[*]}"
fi

if [[ ${#INVALID[@]} -gt 0 ]]; then
  echo
  echo "Rejected:" >&2
  echo "  ${INVALID[*]}" >&2
fi

if [[ ${#INSTALLED[@]} -eq 0 ]]; then
  echo
  echo "Nothing installed; not building." >&2
  exit 1
fi

if [[ "$FLASH" == true ]]; then
  echo
  echo "Building and flashing on $PORT..."
  idf.py build
  idf.py -p "$PORT" flash monitor
else
  echo
  echo "Run 'idf.py build' to rebuild the assets, or pass --flash to build and flash."
fi
