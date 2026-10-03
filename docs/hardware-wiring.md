# WALL-E ESP32-S3 Wiring and Pinout

This document is the final wiring reference for the WALL-E/XiaoZhi voice-assistant enclosure.

## Hardware

- Freenove ESP32-S3 development board
- 2.8-inch ILI9341 SPI TFT display with onboard microSD slot
- INMP441 I2S digital microphone
- MAX98357A I2S mono Class-D amplifier
- Two 8-ohm, 3 W speakers wired in parallel
- Rear panel USB-C male-to-female extension
- Small perfboard used only as a power/I2S distribution hub

## Final GPIO allocation

| Function | ESP32-S3 GPIO | Connected module/pin | Direction / note |
|---|---:|---|---|
| I2S bit clock | GPIO 4 | INMP441 `SCK`; MAX98357A `BCLK` | Shared I2S clock |
| I2S word-select | GPIO 5 | INMP441 `WS`; MAX98357A `LRC`/`LRCLK` | Shared I2S clock |
| Microphone data | GPIO 6 | INMP441 `SD` | Microphone -> ESP32 |
| Amplifier data | GPIO 7 | MAX98357A `DIN` | ESP32 -> amplifier |
| TFT data/command | GPIO 9 | ILI9341 `DC`/`A0` | Display control |
| TFT chip select | GPIO 10 | ILI9341 `CS` | Display SPI chip select |
| SPI MOSI | GPIO 11 | ILI9341 `MOSI`/`SDA`/`DIN`; SD `MOSI`/`DI` | Shared SPI output |
| SPI clock | GPIO 12 | ILI9341 `SCK`/`CLK`; SD `SCK`/`CLK` | Shared SPI clock |
| TFT backlight | GPIO 13 | ILI9341 `LED`/`BL` | Firmware-controlled backlight |
| TFT reset | GPIO 14 | ILI9341 `RST`/`RESET` | Display reset |
| SD-card MISO | GPIO 15 | microSD `MISO`/`DO`/`SDO` | SD card -> ESP32 |
| SD-card chip select | GPIO 16 | microSD `CS`/`SS` | Dedicated SD chip select |
| Boot button | GPIO 0 | Onboard BOOT button | Keep accessible; do not repurpose |
| RGB LED | GPIO 48 | Onboard RGB LED | Built-in LED |

## Firmware configuration

The custom board configuration uses the following display and audio mapping:

```cpp
#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

// Audio: INMP441 microphone + MAX98357A amplifier
// BCLK and WS are shared by both devices.
#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_4  // INMP441 SCK / MAX98357A BCLK
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_5  // INMP441 WS / MAX98357A LRC
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_6  // INMP441 SD -> ESP32
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_7  // ESP32 -> MAX98357A DIN

// Buttons and LED
#define BOOT_BUTTON_GPIO GPIO_NUM_0
#define BUILTIN_LED_GPIO GPIO_NUM_48

// 2.8-inch ILI9341 SPI display
#define DISPLAY_MOSI_PIN GPIO_NUM_11
#define DISPLAY_CLK_PIN GPIO_NUM_12
#define DISPLAY_CS_PIN GPIO_NUM_10
#define DISPLAY_DC_PIN GPIO_NUM_9
#define DISPLAY_RST_PIN GPIO_NUM_14
#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_13

#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define LCD_TYPE_ILI9341_SERIAL

#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 320

#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false

#define DISPLAY_INVERT_COLOR false
#define DISPLAY_RGB_ORDER LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X 0
#define DISPLAY_OFFSET_Y 0
#define DISPLAY_SPI_MODE 0

// Onboard microSD reader: shares MOSI/SCK with the display.
#define SD_MISO_PIN GPIO_NUM_15
#define SD_CS_PIN GPIO_NUM_16

#endif  // _BOARD_CONFIG_H_
```

> Adding `SD_MISO_PIN` and `SD_CS_PIN` to this header only documents the pins. The SD-card driver/init code must also use MOSI=GPIO11, MISO=GPIO15, SCLK=GPIO12, and CS=GPIO16 before the card is usable.

## Per-module wiring

### INMP441 I2S microphone

| INMP441 pin | Connect to |
|---|---|
| `VDD` | ESP32 `3V3` |
| `GND` | ESP32 `GND` |
| `SCK` | GPIO 4 |
| `WS` | GPIO 5 |
| `SD` | GPIO 6 |
| `L/R` | `GND` |

Notes:

- Power the INMP441 from **3.3 V only**.
- `L/R` is tied to GND for the selected microphone channel.
- Keep the mic harness short and away from the amplifier, speaker wiring, and 5 V wiring.

### MAX98357A I2S amplifier

| MAX98357A pin | Connect to |
|---|---|
| `VIN` | ESP32 `5V` / USB 5 V rail |
| `GND` | ESP32 `GND` |
| `BCLK` | GPIO 4 |
| `LRC` / `LRCLK` | GPIO 5 |
| `DIN` | GPIO 7 |
| `GAIN` | Leave in the currently tested working state |
| `SD` | Leave in the currently tested working state |
| `SPK+` | Speaker 1 `+` and Speaker 2 `+` |
| `SPK-` | Speaker 1 `-` and Speaker 2 `-` |

Notes:

- The amplifier uses the ESP32's shared I2S BCLK and WS signals.
- `SPK+` and `SPK-` are bridge-tied outputs. **Neither speaker terminal is ground.**
- Never connect `SPK-` to ESP32 GND, amplifier GND, USB ground, or the enclosure.
- The amplifier is powered from 5 V; it operates independently of the microphone's 3.3 V rail.

### Two speakers

The two 8-ohm speakers are wired **in parallel**, creating a 4-ohm mono load.

```text
MAX98357A SPK+ ──┬── Speaker 1 +
                 └── Speaker 2 +

MAX98357A SPK- ──┬── Speaker 1 -
                 └── Speaker 2 -
```

- Keep polarity consistent on both speakers so they remain in phase.
- Use short twisted speaker-wire pairs.
- This configuration was tested and sounds good.
- Do not add additional speakers in parallel to this amplifier output.

### ILI9341 SPI display

| ILI9341 label | Connect to |
|---|---|
| `VCC` / `VIN` | ESP32 `3V3` |
| `GND` | ESP32 `GND` |
| `MOSI` / `SDA` / `DIN` | GPIO 11 |
| `SCK` / `CLK` | GPIO 12 |
| `CS` | GPIO 10 |
| `DC` / `A0` | GPIO 9 |
| `RST` / `RESET` | GPIO 14 |
| `LED` / `BL` | GPIO 13 |
| `MISO` / `SDO` | GPIO 15, if exposed by the module |

- Keep display logic at 3.3 V.
- The configured BGR color order is intentional for this module.
- Touch-controller pins remain unused unless touch support is explicitly added.

### ILI9341 onboard microSD slot

The display and microSD share the SPI bus. Their MOSI and SCK signals are shared; each has its own chip-select line.

| microSD signal | Connect to | Notes |
|---|---|---|
| `SD_VCC` | ESP32 `3V3` | Usually shared internally with display power |
| `SD_GND` | ESP32 `GND` | Common ground |
| `SD_MOSI` / `DI` | GPIO 11 | Shared with TFT MOSI |
| `SD_SCK` / `CLK` | GPIO 12 | Shared with TFT clock |
| `SD_MISO` / `DO` | GPIO 15 | Data from SD card to ESP32 |
| `SD_CS` / `SS` | GPIO 16 | Dedicated SD card chip select |

On many modules, `MOSI`, `SCK`, power, and ground are already internally shared between the display and SD reader. In that case, add only the externally available `MISO`/`SDO` and `SD_CS` wires beyond the normal TFT wiring.

## Power and perfboard hub

Use a small 10 x 15 mm to 15 x 20 mm perfboard as a distribution hub only. Do not route every signal through it.

| Hub rail/junction | Connections |
|---|---|
| 3.3 V rail | ESP32 `3V3` -> INMP441 `VDD` -> ILI9341 `VCC` / SD power |
| 5 V rail | ESP32 `5V` -> MAX98357A `VIN` |
| Ground rail | ESP32 `GND` -> microphone `GND` -> amplifier `GND` -> display `GND` |
| I2S BCLK split | GPIO 4 -> INMP441 `SCK` and MAX98357A `BCLK` |
| I2S WS split | GPIO 5 -> INMP441 `WS` and MAX98357A `LRC` |

Keep these as direct runs:

- GPIO 6 -> INMP441 `SD`
- GPIO 7 -> MAX98357A `DIN`
- Display SPI and control lines
- SD-card `MISO` and `CS`
- Amplifier speaker output wiring

## Amplifier supply capacitor

Install a local bulk capacitor close to the MAX98357A power pins:

- Preferred permanent part: **470 uF to 1000 uF, 10 V or 16 V, polarized electrolytic**.
- A **1000 uF, 6.3 V** electrolytic can be used for short bench testing on a well-regulated 5 V rail, but use a 10 V or 16 V part in the permanent enclosure for better voltage margin.
- Optional: add a **0.1 uF (100 nF) ceramic capacitor** in parallel for high-frequency bypassing.

Connection:

```text
5V rail ───────────────► MAX98357A VIN
   │
   └──── capacitor positive (+) lead

GND rail ──────────────► MAX98357A GND
   │
   └──── capacitor negative (-, striped) lead
```

The capacitor is connected **in parallel** across `VIN` and `GND`, never in series and never to `SPK+` or `SPK-`.

## Text wiring overview

```text
                              ESP32-S3
                         ┌────────────────┐
3V3 ────────────────────┼─> INMP441 VDD  │
                         ├─> ILI9341 VCC  │
5V ─────────────────────┼─> MAX98357A VIN │
GND ────────────────────┼─> all GND pins  │
                         │                │
GPIO 4 ─ I2S BCLK ──────┼─> Mic SCK       │
                         └─> Amp BCLK      │
GPIO 5 ─ I2S WS ────────┼─> Mic WS        │
                         └─> Amp LRC       │
GPIO 6 <─ Mic SD         │
GPIO 7 ─> Amp DIN        │
                         │
GPIO 9  ─> TFT DC        │
GPIO 10 ─> TFT CS        │
GPIO 11 ─> TFT/SD MOSI   │
GPIO 12 ─> TFT/SD SCK    │
GPIO 13 ─> TFT BL        │
GPIO 14 ─> TFT RST       │
GPIO 15 <─ SD MISO       │
GPIO 16 ─> SD CS         │
                         └────────────────┘

MAX98357A SPK+ ──┬── Speaker 1 +
                 └── Speaker 2 +
MAX98357A SPK- ──┬── Speaker 1 -
                 └── Speaker 2 -
```

## Physical assembly decisions

- Use separate labeled harnesses for microphone, amplifier, display, and speakers.
- Put the microphone behind its exterior holes and route its harness away from the amplifier and speakers.
- Mount the MAX98357A near the speaker split so speaker runs remain short.
- Twist speaker wires; keep the amplifier's 5 V and speaker wiring away from the microphone harness.
- Mount the ESP32 and small perfboard in the rear cavity behind/beside the display.
- Use the rear panel USB-C extension for external power and programming access.
- Add strain relief and insulation before final closure; avoid permanent adhesive until the full system is tested.

## Verification checklist

- [ ] Confirm no short between 5 V and GND.
- [ ] Confirm no short between 3.3 V and GND.
- [ ] Confirm INMP441 receives 3.3 V, not 5 V.
- [ ] Confirm MAX98357A receives 5 V and shares GND with the ESP32.
- [ ] Confirm `SPK-` is not connected to GND.
- [ ] Confirm speaker polarities match.
- [ ] Confirm display works before mounting it permanently.
- [ ] Confirm microphone works with speakers disconnected first.
- [ ] Confirm amplifier works with the two-speaker 4-ohm load.
- [ ] Confirm the capacitor polarity: `+` to VIN/5 V, striped `-` to GND.
- [ ] Confirm microSD support in firmware before relying on the external slot.
- [ ] Run a 10-15 minute audio and voice test before sealing the enclosure.
