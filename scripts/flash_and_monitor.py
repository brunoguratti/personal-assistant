#!/usr/bin/env python3
"""
Interactive helper to scan USB serial ports and run:
  1) Build + Flash + Monitor
  2) Just Monitor

Automatically sources ESP-IDF export.sh if the environment is not active.
"""

import glob
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import List, Optional, Tuple

ROOT = Path(__file__).resolve().parent.parent

# Default ESP-IDF export script location
DEFAULT_IDF_EXPORT_PATHS = [
    Path("~/Projects/esp/esp-idf-v6.0.2/export.sh").expanduser(),
    Path("~/Projects/esp/esp-idf/export.sh").expanduser(),
    Path("~/esp/esp-idf/export.sh").expanduser(),
]

# Common USB serial patterns on macOS and Linux
PORT_PATTERNS = [
    # macOS USB-OTG / USB-JTAG / CP210x / CH340 / FTDI
    "/dev/cu.usbmodem*",
    "/dev/cu.usbserial*",
    "/dev/cu.wchusbserial*",
    "/dev/cu.SLAB_USBtoUART*",
    # Linux
    "/dev/ttyUSB*",
    "/dev/ttyACM*",
]

# Ports to filter out (Bluetooth, internal modem, etc.)
IGNORED_PATTERNS = [
    "*Bluetooth*",
    "*wlan*",
    "*debug*",
    "*AirPods*",
    "*Wireless*",
]


def auto_load_esp_idf() -> bool:
    """Source ESP-IDF environment into current process if not already active."""
    if "IDF_PATH" in os.environ and "IDF_PYTHON_ENV_PATH" in os.environ:
        return True

    for export_path in DEFAULT_IDF_EXPORT_PATHS:
        if export_path.exists():
            print(f"🔧 Automatically loading ESP-IDF from: \033[1;33m{export_path}\033[0m")
            # Run bash to source export.sh and capture all resulting env vars
            cmd = f'. "{export_path}" >/dev/null 2>&1 && env'
            try:
                proc = subprocess.run(
                    ["bash", "-c", cmd],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                    check=False,
                )
                if proc.returncode == 0:
                    for line in proc.stdout.splitlines():
                        if "=" in line:
                            k, _, v = line.partition("=")
                            os.environ[k] = v
                    print("✅ ESP-IDF environment loaded successfully.\n")
                    return True
            except Exception as e:
                print(f"⚠️ Failed to auto-source {export_path}: {e}")

    return False


def scan_serial_ports() -> List[Tuple[str, str]]:
    """Scan for available USB serial ports."""
    # Try pyserial first if available for richer info
    try:
        from serial.tools import list_ports

        ports = []
        for p in list_ports.comports():
            name = p.device
            if any(Path(name).match(ig) for ig in IGNORED_PATTERNS):
                continue
            if p.vid or "USB" in (p.description or "") or "usbmodem" in name or "usbserial" in name:
                desc = f"{p.device} ({p.description or 'USB Device'})"
                ports.append((p.device, desc))

        if ports:
            return ports
    except ImportError:
        pass

    # Fallback to filesystem globbing
    found = []
    for pattern in PORT_PATTERNS:
        for path in sorted(glob.glob(pattern)):
            if not any(Path(path).match(ig) for ig in IGNORED_PATTERNS):
                if path not in [p[0] for p in found]:
                    found.append((path, path))
    return found


def select_port(ports: List[Tuple[str, str]]) -> Optional[str]:
    """Let user confirm or select from available ports."""
    if not ports:
        print("\n❌ No ESP32 USB serial ports detected.")
        print("   - Make sure your ESP32-S3 is connected via USB.")
        print("   - On boards with two USB ports, try the other USB port.")
        return None

    if len(ports) == 1:
        port, desc = ports[0]
        print(f"\n📡 Found 1 USB serial port: \033[1;32m{desc}\033[0m")
        choice = input("Use this port? [Y/n]: ").strip().lower()
        if choice in ("", "y", "yes"):
            return port
        return None

    print(f"\n📡 Multiple USB serial ports found ({len(ports)}):")
    for i, (port, desc) in enumerate(ports, start=1):
        print(f"  [{i}] {desc}")

    while True:
        try:
            choice = input(f"\nSelect port [1-{len(ports)}] (or 'q' to quit): ").strip()
            if choice.lower() == "q":
                return None
            idx = int(choice) - 1
            if 0 <= idx < len(ports):
                return ports[idx][0]
            print(f"Please enter a number between 1 and {len(ports)}.")
        except ValueError:
            print("Invalid input. Please enter a valid number.")


def get_target_board() -> str:
    """Prompt for board or use custom/freenove-s3-2.8-lcd as default."""
    default_board = "custom/freenove-s3-2.8-lcd"
    print(f"\n🎯 Target Board (default: \033[1;36m{default_board}\033[0m)")
    user_board = input(f"Enter board path or press Enter for default: ").strip()
    return user_board if user_board else default_board


def run_command(cmd: List[str], cwd: Path = ROOT) -> int:
    """Execute command in terminal with interactive I/O."""
    cmd_str = " ".join(cmd)
    print(f"\n🚀 Running: \033[1;34m{cmd_str}\033[0m\n")
    try:
        return subprocess.call(cmd, cwd=cwd, env=os.environ)
    except KeyboardInterrupt:
        print("\n\n⏹️ Process interrupted by user.")
        return 130


def main():
    print("=" * 60)
    print("   🤖 XiaoZhi ESP32 Build, Flash & Monitor Assistant")
    print("=" * 60)

    # 1. Ask Action First
    print("\nSelect an action:")
    print("  [1] \033[1;32mBuild + Flash + Monitor\033[0m")
    print("  [2] \033[1;36mJust Monitor\033[0m")
    print("  [q] Quit")

    while True:
        action_choice = input("\nEnter choice [1/2/q]: ").strip().lower()
        if action_choice in ("q", "quit"):
            print("Exiting.")
            sys.exit(0)
        if action_choice in ("1", "2"):
            break
        print("Invalid choice. Please enter 1, 2, or q.")

    # 2. Auto-load ESP-IDF environment if not already loaded
    if not auto_load_esp_idf():
        print("\n⚠️  Could not automatically locate 'export.sh'.")
        print("   Please ensure ESP-IDF is installed at ~/Projects/esp/esp-idf-v6.0.2/")

    # 3. Scan and Select Port
    ports = scan_serial_ports()
    selected_port = select_port(ports)
    if not selected_port:
        sys.exit(1)

    print(f"\n✅ Selected Port: \033[1;32m{selected_port}\033[0m")

    # 4. Execute Selected Action
    if action_choice == "1":
        # Build + Flash + Monitor
        board = get_target_board()

        # Step A: Build with scripts/build.py
        print("\n" + "-" * 50)
        print(f"📦 Step 1/2: Building board '{board}'...")
        print("-" * 50)
        build_rc = run_command([sys.executable, "scripts/build.py", board])
        if build_rc != 0:
            print("\n❌ Build failed! Please check compiler errors above.")
            sys.exit(build_rc)

        # Step B: Flash + Monitor with idf.py
        print("\n" + "-" * 50)
        print(f"⚡ Step 2/2: Flashing & Monitoring on {selected_port}...")
        print("-" * 50)
        flash_rc = run_command(["idf.py", "-p", selected_port, "flash", "monitor"])
        sys.exit(flash_rc)

    elif action_choice == "2":
        # Just Monitor
        print("\n" + "-" * 50)
        print(f"📺 Opening Serial Monitor on {selected_port} (Ctrl+] to exit)...")
        print("-" * 50)
        monitor_rc = run_command(["idf.py", "-p", selected_port, "monitor"])
        sys.exit(monitor_rc)


if __name__ == "__main__":
    main()
