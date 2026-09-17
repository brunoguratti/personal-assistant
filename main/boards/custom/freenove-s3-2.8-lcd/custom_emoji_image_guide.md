# Custom Emoji Image Guide

This guide explains how to replace an existing display emotion/emoji with a custom PNG image in the XiaoZhi ESP-IDF project.

It assumes the project is located at:

```text
/Users/bruno/Projects/personal-assistant
```

The board used here is the custom Freenove ESP32-S3 2.8-inch ILI9341 LCD board. The procedure uses the already working LVGL image route:

```text
Application state
  -> display->SetEmotion("custom_avatar")
  -> LcdDisplay::SetEmotion()
  -> existing emoji_image_ LVGL widget
  -> custom RGB565A8 C asset
```

Do not create a separate LVGL image object for the custom avatar unless there is a specific reason to do so. Reusing `emoji_image_` avoids z-order conflicts with the stock emoji and keeps display locking in the existing UI path.

## 1. Prepare the PNG

Use a **square PNG with a transparent background**.

Recommended requirements:

| Property | Value |
|---|---|
| Final image size | 200 x 200 px |
| File format | PNG |
| Color | RGBA |
| Background | Fully transparent |
| Subject | Centered, with 12–20 px transparent padding |
| Style | High contrast, bold shapes, minimal subtle gradients |

The ILI9341 is a 240 x 320 portrait display, but the existing emoji widget has been tested successfully with 200 x 200 custom avatars.

Check a source PNG:

```bash
sips -g pixelWidth -g pixelHeight -g hasAlpha assets-src/wall-e/my_avatar.png
```

If the image is not already 200 x 200, resize it before conversion:

```bash
mkdir -p /tmp/custom-avatar

sips --resampleWidth 200 \
  assets-src/wall-e/my_avatar.png \
  --out /tmp/custom-avatar/my_avatar-200x200.png
```

Check the resized output:

```bash
sips -g pixelWidth -g pixelHeight -g hasAlpha \
  /tmp/custom-avatar/my_avatar-200x200.png
```

Expected:

```text
pixelWidth: 200
pixelHeight: 200
hasAlpha: yes
```

If the original is not square, create a square transparent canvas in an image editor first. Do not stretch a portrait or landscape character into a square.

## 2. Choose names

Use one internal emotion name and one C symbol.

Example for a custom idle avatar:

```text
Emotion string: custom_idle
C symbol:       custom_idle
C source:       main/display/custom_idle.c
C header:       main/display/custom_idle.h
```

Names should use lowercase letters, numbers, and underscores. The emotion string must match exactly in the application state handler and `LcdDisplay::SetEmotion()`.

## 3. Convert PNG to an LVGL C asset

Use the LVGL converter that is bundled with the project’s managed LVGL component:

```bash
cd /Users/bruno/Projects/personal-assistant

mkdir -p /tmp/custom-idle-lvgl

python managed_components/lvgl__lvgl/scripts/LVGLImage.py \
  /tmp/custom-avatar/my_avatar-200x200.png \
  --ofmt C \
  --cf RGB565A8 \
  --compress NONE \
  --name custom_idle \
  -o /tmp/custom-idle-lvgl
```

Important settings:

| Converter argument | Required value | Purpose |
|---|---|---|
| `--ofmt` | `C` | Create a C source asset compiled into firmware |
| `--cf` | `RGB565A8` | RGB565 color plus alpha; keeps transparent background |
| `--compress` | `NONE` | Simple, reliable first test |
| `--name` | `custom_idle` | Defines the C image symbol |
| `-o` | Output directory | The converter expects a directory, not a `.c` filename |

Do not use `RGB565` or `RGB565_SWAPPED` for a transparent avatar. Those formats do not carry alpha and will render an opaque rectangle/background.

Optionally add `--rgb565dither` for smooth gradients. For deliberately crisp pixel art, compare with and without dithering.

## 4. Verify the generated asset

Before copying it into the project, verify the format and dimensions:

```bash
grep -n -E 'LV_COLOR_FORMAT|\.w =|\.h =|\.stride =' \
  /tmp/custom-idle-lvgl/custom_idle.c | tail -12
```

Expected essentials:

```cpp
.cf = LV_COLOR_FORMAT_RGB565A8,
.w = 200,
.h = 200,
.stride = 400,
```

`stride = 400` is normal: the RGB565 color plane uses 2 bytes per pixel, so `200 x 2 = 400` bytes per row. The alpha plane is stored separately by the RGB565A8 format.

A 200 x 200 RGB565A8 asset contains approximately:

```text
200 x 200 x 3 = 120,000 bytes
```

Do not convert a large original image directly. For example, a 1254 x 1254 RGB565A8 image would need approximately 4.5 MiB of raw image data and can make the app binary exceed the OTA app partition.

## 5. Add source and header

Copy the generated C source into the display directory:

```bash
cp /tmp/custom-idle-lvgl/custom_idle.c main/display/custom_idle.c
```

Create the matching header:

```bash
cat > main/display/custom_idle.h <<'EOF'
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

extern const lv_image_dsc_t custom_idle;

#ifdef __cplusplus
}
#endif
EOF
```

Verify the descriptor declaration in the generated source:

```bash
grep -n 'const lv_.*_dsc_t custom_idle' main/display/custom_idle.c
```

If it uses `lv_image_dsc_t`, the header above is correct. If it instead uses `lv_img_dsc_t`, use that same type in the header.

## 6. Add the C source to CMake

Open:

```text
main/CMakeLists.txt
```

Add the custom C asset in the `SOURCES` list, near `display/lcd_display.cc`:

```cmake
"display/custom_idle.c"
```

Example:

```cmake
"display/lcd_display.cc"
"display/custom_idle.c"
"display/oled_display.cc"
```

Do not add the header file to `SOURCES`.

## 7. Route the custom emotion in LcdDisplay

Open:

```text
main/display/lcd_display.cc
```

Add the asset header near the other includes:

```cpp
#include "custom_idle.h"
```

Find:

```cpp
void LcdDisplay::SetEmotion(const char* emotion)
```

After the existing `emoji_image_ == nullptr` check and before the normal `emoji_collection` lookup, add a special case:

```cpp
if (emotion != nullptr && strcmp(emotion, "custom_idle") == 0) {
    DisplayLockGuard lock(this);

    if (gif_controller_) {
        gif_controller_->Stop();
        gif_controller_.reset();
    }

    lv_image_set_src(emoji_image_, &custom_idle);
    lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
    return;
}
```

Use the project’s existing `DisplayLockGuard`. Do not add a separate LVGL object or call `lv_obj_move_foreground()`.

A simplified function layout is:

```cpp
void LcdDisplay::SetEmotion(const char* emotion) {
    // Existing setup and null checks remain unchanged.

    if (emotion != nullptr && strcmp(emotion, "custom_idle") == 0) {
        DisplayLockGuard lock(this);
        if (gif_controller_) {
            gif_controller_->Stop();
            gif_controller_.reset();
        }
        lv_image_set_src(emoji_image_, &custom_idle);
        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // Existing normal emoji/glyph code remains unchanged.
}
```

## 8. Replace an existing emotion call

Find the code that selects the original emotion. For device states, this is typically in:

```text
main/application.cc
```

Search:

```bash
grep -n -A 120 -B 20 'HandleStateChangedEvent' main/application.cc
```

An idle state often looks like:

```cpp
case kDeviceStateIdle:
    display->SetStatus(Lang::Strings::STANDBY);
    display->ClearChatMessages();
    display->SetEmotion("neutral");
    break;
```

Replace only the target emotion name:

```cpp
case kDeviceStateIdle:
    display->SetStatus(Lang::Strings::STANDBY);
    display->ClearChatMessages();
    display->SetEmotion("custom_idle");
    break;
```

Do not add LVGL calls to `Application::SetDeviceState()`. The state wrapper may run outside the normal display/UI flow. Use the established UI handler such as `HandleStateChangedEvent()` and call `display->SetEmotion()` there.

## 9. Build and flash

Activate ESP-IDF if necessary:

```bash
source /Users/bruno/Projects/esp/esp-idf-v6.0.2/export.sh
```

Build:

```bash
cd /Users/bruno/Projects/personal-assistant
idf.py build
```

Flash and monitor:

```bash
idf.py -p /dev/cu.usbmodem5C630518811 flash monitor
```

Use the serial port shown by your machine. The port may change after reconnecting the board.

A successful result should show the custom image when the state enters the emotion you changed, without a watchdog reset and without a white background.

## 10. Quick test by reusing wall_e_idle

For rapid avatar experiments, reuse the existing working `wall_e_idle` symbol instead of adding new code.

The project includes a helper script:

```bash
./scripts/test_wall_e_idle_avatar.sh assets-src/wall-e/my_avatar.png --flash
```

The helper script:

- Resizes a square PNG to 200 x 200
- Generates `RGB565A8`
- Uses the symbol `wall_e_idle`
- Backs up the current `main/display/wall_e_idle.c`
- Replaces the active asset
- Builds the firmware
- Flashes and opens the monitor when `--flash` is passed

For a permanent named custom emotion, follow sections 2 through 9 instead.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| White rectangle behind image | Image is opaque or source PNG has no alpha | Use a transparent PNG and convert with `--cf RGB565A8` |
| Standard emoji still covers custom avatar | A separate image object was added below the emoji layer | Reuse `emoji_image_` through `SetEmotion()` |
| Watchdog reset around `lv_obj_move_foreground()` | UI objects are being reordered from the wrong task/locking path | Remove separate object/foreground code; use existing `SetEmotion()` route |
| Build says all app partitions are too small | Source image was too large before conversion | Resize to 200 x 200 before converting; verify `.w` and `.h` in generated C file |
| Custom image does not appear | Emotion string/symbol mismatch or C asset missing from CMake | Check `custom_idle` spelling, header include, CMake `SOURCES`, and state handler call |
| Colors differ slightly from the PNG | Normal RGB565 conversion and panel calibration | Keep RGB565A8; optionally try `--rgb565dither`; use stronger, flatter color blocks in artwork |
| Red and blue are swapped | LCD RGB/BGR panel order is wrong | Check board `config.h` `DISPLAY_RGB_ORDER`; change only after confirming an obvious red/blue swap |

## Minimal checklist

```text
[ ] PNG is square, 200 x 200, and has transparency
[ ] Converted with --ofmt C --cf RGB565A8 --name custom_idle
[ ] Generated C descriptor says LV_COLOR_FORMAT_RGB565A8
[ ] custom_idle.c is in main/display/
[ ] display/custom_idle.c is listed in main/CMakeLists.txt SOURCES
[ ] custom_idle.h declares the same descriptor type/symbol
[ ] lcd_display.cc includes custom_idle.h
[ ] LcdDisplay::SetEmotion() handles "custom_idle" using emoji_image_
[ ] application.cc calls display->SetEmotion("custom_idle") at the desired state
[ ] idf.py build succeeds
[ ] Firmware is flashed and tested
```
