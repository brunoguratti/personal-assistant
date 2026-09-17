#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="/Users/bruno/Projects/personal-assistant"
CONVERTER="$PROJECT_DIR/managed_components/lvgl__lvgl/scripts/LVGLImage.py"
ACTIVE_ASSET="$PROJECT_DIR/main/display/wall_e_idle.c"
BACKUP_DIR="$PROJECT_DIR/assets-src/wall-e/backups"
OUTPUT_DIR="/tmp/wall-e-idle-lvgl"
TARGET_SIZE=200
PORT="${ESP_PORT:-/dev/cu.usbmodem5C630518811}"

usage() {
  cat <<'EOF'
Usage:
  ./scripts/test_wall_e_idle_avatar.sh IMAGE.png [--flash] [--dither]

Examples:
  ./scripts/test_wall_e_idle_avatar.sh assets-src/wall-e/robodog.png
  ./scripts/test_wall_e_idle_avatar.sh assets-src/wall-e/robodog-refined-200x200.png --flash
  ESP_PORT=/dev/cu.usbmodemXXXX ./scripts/test_wall_e_idle_avatar.sh my-avatar.png --flash --dither

What it does:
  1. Resizes the input PNG to 200x200 while preserving alpha.
  2. Converts it to an LVGL RGB565A8 C image named wall_e_idle.
  3. Backs up the current main/display/wall_e_idle.c.
  4. Replaces main/display/wall_e_idle.c.
  5. Builds the firmware; --flash also flashes and opens the monitor.

The input image must be a PNG with an alpha channel if you want a transparent background.
EOF
}

if [[ $# -lt 1 ]]; then
  usage
  exit 1
fi

INPUT="$1"
shift
FLASH=false
DITHER=false

while [[ $# -gt 0 ]]; do
  case "$1" in
    --flash)
      FLASH=true
      ;;
    --dither)
      DITHER=true
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Error: unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
  shift
done

if [[ ! -d "$PROJECT_DIR" ]]; then
  echo "Error: project directory not found: $PROJECT_DIR" >&2
  exit 1
fi

if [[ ! -f "$CONVERTER" ]]; then
  echo "Error: LVGL converter not found: $CONVERTER" >&2
  exit 1
fi

if [[ ! -f "$INPUT" ]]; then
  if [[ -f "$PROJECT_DIR/$INPUT" ]]; then
    INPUT="$PROJECT_DIR/$INPUT"
  else
    echo "Error: image not found: $INPUT" >&2
    exit 1
  fi
fi

case "${INPUT##*.}" in
  png|PNG) ;;
  *)
    echo "Error: use a PNG file so transparency can be preserved." >&2
    exit 1
    ;;
esac

cd "$PROJECT_DIR"

SOURCE_INFO="$(sips -g pixelWidth -g pixelHeight -g hasAlpha "$INPUT")"
printf '%s\n' "$SOURCE_INFO"

if ! grep -q 'hasAlpha: yes' <<<"$SOURCE_INFO"; then
  echo "Warning: the input PNG has no alpha channel. It will render with an opaque background."
fi

SAFE_NAME="$(basename "$INPUT")"
SAFE_NAME="${SAFE_NAME%.*}"
SAFE_NAME="${SAFE_NAME//[^A-Za-z0-9._-]/_}"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
RESIZED="/tmp/wall-e-idle-${SAFE_NAME}-${TARGET_SIZE}x${TARGET_SIZE}.png"

rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR" "$BACKUP_DIR"

# sips preserves the square aspect ratio of standard avatar sources. For non-square
# sources, it scales to TARGET_SIZE wide; crop/pad externally if a square composition is desired.
sips --resampleWidth "$TARGET_SIZE" "$INPUT" --out "$RESIZED" >/dev/null

RESIZED_INFO="$(sips -g pixelWidth -g pixelHeight -g hasAlpha "$RESIZED")"
printf '%s\n' "$RESIZED_INFO"

if ! grep -q "pixelWidth: $TARGET_SIZE" <<<"$RESIZED_INFO"; then
  echo "Error: resize did not produce ${TARGET_SIZE}px width." >&2
  exit 1
fi

if ! grep -q "pixelHeight: $TARGET_SIZE" <<<"$RESIZED_INFO"; then
  echo "Error: input is not square. The resized image is not ${TARGET_SIZE}x${TARGET_SIZE}." >&2
  echo "Create a square transparent PNG first, then run this script again." >&2
  exit 1
fi

CONVERTER_ARGS=(
  "$RESIZED"
  --ofmt C
  --cf RGB565A8
  --compress NONE
  --name wall_e_idle
  -o "$OUTPUT_DIR"
)

if [[ "$DITHER" == true ]]; then
  CONVERTER_ARGS+=(--rgb565dither)
fi

python "$CONVERTER" "${CONVERTER_ARGS[@]}"

GENERATED="$OUTPUT_DIR/wall_e_idle.c"
if [[ ! -f "$GENERATED" ]]; then
  echo "Error: expected generated asset was not created: $GENERATED" >&2
  exit 1
fi

if ! grep -q 'LV_COLOR_FORMAT_RGB565A8' "$GENERATED"; then
  echo "Error: generated asset is not RGB565A8; refusing to replace active asset." >&2
  exit 1
fi

if ! grep -q "\.w = $TARGET_SIZE," "$GENERATED" || ! grep -q "\.h = $TARGET_SIZE," "$GENERATED"; then
  echo "Error: generated asset is not ${TARGET_SIZE}x${TARGET_SIZE}; refusing to replace active asset." >&2
  exit 1
fi

if [[ -f "$ACTIVE_ASSET" ]]; then
  cp "$ACTIVE_ASSET" "$BACKUP_DIR/wall_e_idle-${TIMESTAMP}-before-${SAFE_NAME}.c"
fi

cp "$GENERATED" "$ACTIVE_ASSET"

printf '\nInstalled avatar source:\n  %s\n' "$INPUT"
printf 'Generated asset:\n  %s\n' "$ACTIVE_ASSET"
printf 'Backup directory:\n  %s\n\n' "$BACKUP_DIR"

echo "Verified: RGB565A8, ${TARGET_SIZE}x${TARGET_SIZE}, symbol wall_e_idle."
echo "Building firmware..."
idf.py build

if [[ "$FLASH" == true ]]; then
  echo "Flashing on $PORT and opening monitor..."
  idf.py -p "$PORT" flash monitor
else
  echo
  echo "Build complete. To flash and monitor, run:"
  echo "  idf.py -p $PORT flash monitor"
  echo
  echo "Or use next time:"
  echo "  ./scripts/test_wall_e_idle_avatar.sh <image.png> --flash"
fi
