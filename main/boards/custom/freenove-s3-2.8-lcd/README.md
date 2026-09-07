# Custom Freenove ESP32-S3 + 2.8" ILI9341 LCD (INMP441 & MAX98357A)

This custom board configuration is for the standalone **Freenove ESP32-S3 Dev Board** connected to an **ILI9341 2.8" TFT SPI LCD**, **INMP441 I2S Microphone**, and **MAX98357A I2S Amplifier**.

---

## Wiring Pinout Guide

### 1. I2S Audio Wiring

| Signal                           | ESP32-S3 GPIO | INMP441 (Microphone) | MAX98357A (Amplifier) |
| -------------------------------- | ------------- | -------------------- | --------------------- |
| **BCLK (Bit Clock)**             | `GPIO 4`      | `SCK`                | `BCLK`                |
| **WS / LRC (Word Select)**       | `GPIO 5`      | `WS`                 | `LRC`                 |
| **DIN (Data In / Mic Out)**      | `GPIO 6`      | `SD`                 | —                     |
| **DOUT (Data Out / Speaker In)** | `GPIO 7`      | —                    | `DIN`                 |
| **Power (3.3V)**                 | `3V3`         | `VDD`                | —                     |
| **Power (5V / VIN)**             | `5V / VIN`    | —                    | `VIN`                 |
| **GND**                          | `GND`         | `GND` / `L/R (GND)`  | `GND`                 |

_Note: INMP441 `L/R` pin should be tied to `GND` (Left Channel)._

---

### 2. Display Wiring (ILI9341 2.8" SPI)

| Signal                   | ESP32-S3 GPIO                                  | ILI9341 Display Pin |
| ------------------------ | ---------------------------------------------- | ------------------- |
| **MOSI (SPI Data)**      | `GPIO 11`                                      | `MOSI / SDI`        |
| **SCK (SPI Clock)**      | `GPIO 12`                                      | `SCK / CLK`         |
| **CS (Chip Select)**     | `GPIO 10`                                      | `CS`                |
| **DC (Data/Command)**    | `GPIO 9`                                       | `DC / RS`           |
| **RESET**                | `GPIO 14`                                      | `RESET / RST`       |
| **Backlight (BL / LED)** | `GPIO 13`                                      | `LED / BLK`         |
| **VCC**                  | `3V3` or `5V` (check module voltage regulator) | `VCC`               |
| **GND**                  | `GND`                                          | `GND`               |

---

### 3. Onboard Controls

- **Boot Button**: `GPIO 0` (Click to toggle chat or enter Wi-Fi config mode).
- **RGB / Built-in LED**: `GPIO 48`.

---

## Build Command

```bash
python3 scripts/build.py custom/freenove-s3-2.8-lcd
```
