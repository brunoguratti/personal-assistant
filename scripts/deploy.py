#!/usr/bin/env python3
"""Interactive helper to scan USB serial ports and run:
  1) Build + Flash + Monitor
  2) Just Monitor

Automatically sources ESP-IDF export.sh when necessary and always uses the
ESP-IDF Python interpreter for the board build.
"""

import glob
import os
import subprocess
import sys
from pathlib import Path
from typing import List, Optional, Tuple

SCRIPT_DIR = Path(__file__).resolve().parent
ROOT = SCRIPT_DIR.parent
BUILD_LOG_DIR = ROOT / "build" / "log"

DEFAULT_IDF_EXPORT_PATHS = [
    Path("~/Projects/esp/esp-idf-v6.0.2/export.sh").expanduser(),
    Path("~/Projects/esp/esp-idf/export.sh").expanduser(),
    Path("~/esp/esp-idf/export.sh").expanduser(),
]

PORT_PATTERNS = [
    "/dev/cu.usbmodem*",
    "/dev/cu.usbserial*",
    "/dev/cu.wchusbserial*",
    "/dev/cu.SLAB_USBtoUART*",
    "/dev/ttyUSB*",
    "/dev/ttyACM*",
]

IGNORED_PATTERNS = [
    "*Bluetooth*",
    "*wlan*",
    "*debug*",
    "*AirPods*",
    "*Wireless*",
]


def auto_load_esp_idf() -> bool:
    """Load ESP-IDF environment variables into this process."""
    if "IDF_PATH" in os.environ and "IDF_PYTHON_ENV_PATH" in os.environ:
        return True

    for export_path in DEFAULT_IDF_EXPORT_PATHS:
        if not export_path.exists():
            continue

        print(f"🔧 Automatically loading ESP-IDF from: \033[1;33m{export_path}\033[0m")
        command = f'. "{export_path}" >/dev/null 2>&1 && env'

        try:
            process = subprocess.run(
                ["bash", "-c", command],
                capture_output=True,
                text=True,
                check=False,
            )
        except OSError as exc:
            print(f"⚠️ Failed to auto-source {export_path}: {exc}")
            continue

        if process.returncode != 0:
            print(f"⚠️ ESP-IDF export failed for {export_path}")
            continue

        for line in process.stdout.splitlines():
            if "=" in line:
                key, _, value = line.partition("=")
                os.environ[key] = value

        print("✅ ESP-IDF environment loaded successfully.\n")
        return True

    return False


def get_idf_python() -> Optional[str]:
    """Return ESP-IDF's Python executable, never the active Conda Python."""
    env_path = os.environ.get("IDF_PYTHON_ENV_PATH")

    if env_path:
        candidates = [
            Path(env_path) / "bin" / "python",
            Path(env_path) / "bin" / "python3",
            Path(env_path) / "Scripts" / "python.exe",
        ]
        for candidate in candidates:
            if candidate.is_file() and os.access(candidate, os.X_OK):
                return str(candidate)

    idf_path = os.environ.get("IDF_PATH")
    if idf_path:
        tools_python = Path(idf_path).parent.parent / ".espressif" / "python_env"
        if tools_python.is_dir():
            for candidate in sorted(tools_python.glob("idf*_env/bin/python"), reverse=True):
                if candidate.is_file() and os.access(candidate, os.X_OK):
                    return str(candidate)

    return None


def scan_serial_ports() -> List[Tuple[str, str]]:
    """Scan for available USB serial ports."""
    try:
        from serial.tools import list_ports

        ports = []
        for port_info in list_ports.comports():
            name = port_info.device
            if any(Path(name).match(pattern) for pattern in IGNORED_PATTERNS):
                continue
            if (
                port_info.vid
                or "USB" in (port_info.description or "")
                or "usbmodem" in name
                or "usbserial" in name
            ):
                description = f"{port_info.device} ({port_info.description or 'USB Device'})"
                ports.append((port_info.device, description))

        if ports:
            return ports
    except ImportError:
        pass

    found = []
    for pattern in PORT_PATTERNS:
        for path in sorted(glob.glob(pattern)):
            if not any(Path(path).match(item) for item in IGNORED_PATTERNS):
                if path not in [existing[0] for existing in found]:
                    found.append((path, path))
    return found


def select_port(ports: List[Tuple[str, str]]) -> Optional[str]:
    """Let the user confirm or select from discovered ports."""
    if not ports:
        print("\n❌ No ESP32 USB serial ports detected.")
        print("   - Make sure your ESP32-S3 is connected via USB.")
        print("   - On boards with two USB ports, try the other USB port.")
        return None

    if len(ports) == 1:
        port, description = ports[0]
        print(f"\n📡 Found 1 USB serial port: \033[1;32m{description}\033[0m")
        choice = input("Use this port? [Y/n]: ").strip().lower()
        return port if choice in ("", "y", "yes") else None

    print(f"\n📡 Multiple USB serial ports found ({len(ports)}):")
    for index, (_, description) in enumerate(ports, start=1):
        print(f"  [{index}] {description}")

    while True:
        choice = input(f"\nSelect port [1-{len(ports)}] (or 'q' to quit): ").strip()
        if choice.lower() == "q":
            return None
        try:
            index = int(choice) - 1
            if 0 <= index < len(ports):
                return ports[index][0]
        except ValueError:
            pass
        print(f"Please enter a number between 1 and {len(ports)}.")


def get_target_board() -> str:
    """Prompt for a board path, defaulting to Bruno's Freenove S3 board."""
    default_board = "custom/freenove-s3-2.8-lcd"
    print(f"\n🎯 Target Board (default: \033[1;36m{default_board}\033[0m)")
    board = input("Enter board path or press Enter for default: ").strip()
    return board or default_board


def run_command(command: List[str], cwd: Path = ROOT) -> int:
    """Execute an interactive subprocess using the prepared ESP-IDF environment."""
    display_command = " ".join(command)
    print(f"\n🚀 Running: \033[1;34m{display_command}\033[0m\n")
    try:
        return subprocess.call(command, cwd=cwd, env=os.environ.copy())
    except KeyboardInterrupt:
        print("\n\n⏹️ Process interrupted by user.")
        return 130


def show_build_errors() -> None:
    """Print likely compiler/linker errors from the newest ESP-IDF build logs."""
    if not BUILD_LOG_DIR.is_dir():
        print(f"\nNo ESP-IDF log directory found: {BUILD_LOG_DIR}")
        return

    log_files = sorted(
        BUILD_LOG_DIR.glob("idf_py_*output_*"),
        key=lambda path: path.stat().st_mtime,
        reverse=True,
    )[:2]

    if not log_files:
        print(f"\nNo idf.py output logs found in: {BUILD_LOG_DIR}")
        return

    print("\n" + "-" * 50)
    print("Recent build errors")
    print("-" * 50)

    command = [
        "grep",
        "-nEi",
        r"error:|fatal error:|undefined reference|not declared|failed",
        *map(str, log_files),
    ]

    try:
        subprocess.run(command, cwd=ROOT, check=False)
    except OSError as exc:
        print(f"Could not read build logs: {exc}")

def main() -> None:
    print("=" * 60)
    print("   XiaoZhi ESP32 Build, Flash & Monitor Assistant")
    print("=" * 60)

    print("\nSelect an action:")
    print("  [1] Build + Flash + Monitor")
    print("  [2] Build only")
    print("  [3] Flash only")
    print("  [4] Monitor only")
    print("  [q] Quit")

    while True:
        action = input("\nEnter choice [1/2/3/4/q]: ").strip().lower()
        if action in ("q", "quit"):
            print("Exiting.")
            return
        if action in ("1", "2", "3", "4"):
            break
        print("Invalid choice. Please enter 1, 2, 3, 4, or q.")

    if not auto_load_esp_idf():
        print("\nCould not automatically load ESP-IDF export.sh.")
        print("Expected ESP-IDF at ~/Projects/esp/esp-idf-v6.0.2/")
        sys.exit(1)

    idf_python = get_idf_python()
    if not idf_python:
        print("\nCould not locate the ESP-IDF Python interpreter.")
        print("IDF_PYTHON_ENV_PATH was not set to a valid environment.")
        sys.exit(1)

    print(f"Using ESP-IDF Python: {idf_python}")

    # Only Build does not need an ESP32 plugged in.
    if action == "2":
        board = get_target_board()

        print("\n" + "-" * 50)
        print(f"Building board '{board}'...")
        print("-" * 50)

        sys.exit(
            run_command(
                [idf_python, "scripts/build.py", board],
            )
        )

    # Flash and monitor require a serial port.
    selected_port = select_port(scan_serial_ports())
    if not selected_port:
        sys.exit(1)

    print(f"\nSelected Port: {selected_port}")

    # Build + Flash + Monitor
    if action == "1":
        board = get_target_board()

        print("\n" + "-" * 50)
        print(f"Step 1/2: Building board '{board}'...")
        print("-" * 50)

        build_return_code = run_command(
            [idf_python, "scripts/build.py", board],
        )
        if build_return_code != 0:
            print("\nBuild failed. Fix the compiler errors before flashing.")
            show_build_errors()
            sys.exit(build_return_code)

        print("\n" + "-" * 50)
        print(f"Step 2/2: Flashing and monitoring {selected_port}...")
        print("-" * 50)

        sys.exit(
            run_command(
                ["idf.py", "-p", selected_port, "flash", "monitor"],
            )
        )

    # Flash only
    if action == "3":
        print("\n" + "-" * 50)
        print(f"Flashing {selected_port}...")
        print("-" * 50)

        sys.exit(
            run_command(
                ["idf.py", "-p", selected_port, "flash"],
            )
        )

    # Monitor only
    print("\n" + "-" * 50)
    print(f"Opening serial monitor on {selected_port} (Ctrl+] to exit)...")
    print("-" * 50)

    sys.exit(
        run_command(
            ["idf.py", "-p", selected_port, "monitor"],
        )
    )


if __name__ == "__main__":
    main()