# Personal Voice Assistant

A personal fork of [XiaoZhi](https://github.com/78/xiaozhi-esp32), an ESP-IDF
voice-assistant firmware. This fork targets a single custom board and adds a few
personal features on top of the upstream project.

> Upstream XiaoZhi code that is not used by this board was archived under
> `_upstream/` (git-ignored) to keep the working tree focused. See
> [Repository layout](#repository-layout).

## Hardware

The only supported build is the **Custom Freenove ESP32-S3 + 2.8" ILI9341 LCD**
board:

| Part              | Detail                                                        |
| ----------------- | ------------------------------------------------------------- |
| MCU               | Freenove ESP32-S3 Dev Board (16 MB flash, octal PSRAM)        |
| Display           | ILI9341 2.8" SPI TFT, 240x320, BGR, backlight on `GPIO 13`    |
| Microphone        | INMP441 I2S                                                   |
| Amplifier         | MAX98357A I2S                                                 |
| Controls          | BOOT button (`GPIO 0`), onboard RGB LED (`GPIO 48`)           |
| Storage (onboard) | microSD sharing the display SPI bus                           |

Audio and display pin assignments live in
`main/boards/custom/freenove-s3-2.8-lcd/config.h`. The complete enclosure
wiring reference (GPIO allocation, per-module hookups, power/capacitor notes,
and a pre-power checklist) is in
[docs/hardware-wiring.md](docs/hardware-wiring.md); the
[board README](main/boards/custom/freenove-s3-2.8-lcd/README.md) has a short
pinout summary.

## Features

- Offline voice wake-up with [ESP-SR](https://github.com/espressif/esp-sr) and
  Opus audio streaming.
- Two transports: [WebSocket](https://github.com/78/xiaozhi-esp32/blob/main/docs/websocket.md)
  and [MQTT + UDP](https://github.com/78/xiaozhi-esp32/blob/main/docs/mqtt-udp.md).
- Wi-Fi provisioning via hotspot or ESP-BluFi.
- 2.8" LCD UI with a custom 21-image emoji set and selectable chat styles.
- **Device timer**: countdown/alarm controllable by voice and from the web UI.
- **Embedded status web server** running on the device once Wi-Fi is connected:
  health dashboard, tools page, timer page, and JSON control APIs.
- **Screen power-save**: the backlight turns off after 60 s of inactivity; the
  BOOT button (or any wake event) restores it.
- **Custom MCP tools** framework with a step-by-step authoring guide
  ([docs/custom-tool.md](docs/custom-tool.md)).

## Repository layout

```
main/
  application.*              Main event loop and protocol lifecycle
  status_web_server.*        Embedded HTTP status/control server
  tools/device_timer.*       Device timer + MCP tools
  boards/
    common/                  Reusable board interfaces and helpers
    custom/freenove-s3-2.8-lcd/   This project's board (config, driver, emoji)
  audio/  display/  protocols/ ...  Upstream core, shared by the board
scripts/
  build.py                   Canonical build entry point
  deploy.py                  Interactive build + flash + monitor helper
  install_custom_emoji.sh    Validate/resize/install custom emoji PNGs
docs/
  hardware-wiring.md         Full enclosure wiring reference and checklist
  custom-tool.md             Guide for adding custom MCP tools
_upstream/                   Archived upstream boards/docs/sdkconfig (git-ignored)
```

## Build and flash

Install [ESP-IDF v6.0.2](https://github.com/espressif/esp-idf/releases/tag/v6.0.2)
and source its environment first:

```sh
source /path/to/esp-idf/export.sh
```

Build the board:

```sh
python3 scripts/build.py custom/freenove-s3-2.8-lcd
```

Then flash and monitor:

```sh
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

Or use the interactive helper, which discovers serial ports and can
build + flash + monitor in one step:

```sh
python3 scripts/deploy.py
```

## Configuration

Run `idf.py menuconfig` (or edit `sdkconfig.defaults.local`, which is
git-ignored) to set the personal options under **Xiaozhi Assistant**:

| Option                                    | Purpose                                                              |
| ----------------------------------------- | -------------------------------------------------------------------- |
| `WALL_E_CONTROL_TOKEN`                    | Bearer token for authenticated control endpoints. Empty disables them. |
| `WALL_E_ANNOUNCEMENT_AUDIO_BASE_URL`      | Trusted LAN prefix (ending in `/audio/`) for remote announcements.    |
| `PERSONAL_GATEWAY_BASE_URL` / `..._TOKEN` | Local gateway endpoint used by the personal MCP setup.                |

`secrets.h` (when a custom tool needs API keys) is git-ignored.

## Status web server

Open `http://<device-ip>/` once the device is on Wi-Fi.

| Route                   | Method      | Auth | Description                              |
| ----------------------- | ----------- | ---- | ---------------------------------------- |
| `/`                     | GET         | no   | Health dashboard                         |
| `/tools`                | GET         | no   | Tools overview                           |
| `/tools/timer`          | GET         | no   | Timer UI                                 |
| `/api/health`           | GET         | no   | Heap/PSRAM, uptime, chip, reset reason   |
| `/api/timer`            | GET/POST/DELETE | no | Read / start / cancel the device timer |
| `/api/display/message`  | POST        | yes  | Show a short message on the LCD          |
| `/api/announce`         | POST        | yes  | Play an announcement from the trusted URL prefix |
| `/api/settings`         | GET/POST    | yes  | Read / set volume and backlight brightness |

Authenticated routes expect `Authorization: Bearer <WALL_E_CONTROL_TOKEN>`.

## Custom emojis

Replace the board's emoji set with square, transparent PNGs named after each
emotion:

```sh
./scripts/install_custom_emoji.sh /path/to/pngs --flash
```

See `main/boards/custom/freenove-s3-2.8-lcd/custom_emoji_image_guide.md` for
details.

## Credits and license

Based on [XiaoZhi](https://github.com/78/xiaozhi-esp32) by Shenzhen Xinzhi
Future Technology Co., Ltd. and released under the MIT License; see
[LICENSE](LICENSE).
