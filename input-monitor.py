#!/usr/bin/env python3
"""
Input monitor for Upwork activity tracking on Wayland.
Monitors keyboard and mouse events from /dev/input/ and writes counts to files.
Requires user to be in the 'input' group.
"""

import os
import sys
import struct
import select
import time
from pathlib import Path

# Input event types (from linux/input-event-codes.h)
EV_KEY = 0x01  # Key/button event
EV_REL = 0x02  # Relative movement (mouse)
EV_ABS = 0x03  # Absolute movement (touchpad/touchscreen)

# Key states
KEY_PRESS = 1
KEY_RELEASE = 0

# Output files
KEYBOARD_COUNT_FILE = "/tmp/upwork-keyboard-count"
MOUSE_COUNT_FILE = "/tmp/upwork-mouse-count"
IDLE_FILE = "/tmp/upwork-idle-ms"
LAST_ACTIVITY_FILE = "/tmp/upwork-last-activity"

# Input event structure: struct input_event { time, type, code, value }
# On 64-bit: 8 bytes timeval.sec + 8 bytes timeval.usec + 2 bytes type + 2 bytes code + 4 bytes value = 24 bytes
EVENT_SIZE = 24
EVENT_FORMAT = 'llHHi'


def find_input_devices():
    """Find keyboard and mouse input devices."""
    keyboards = []
    mice = []

    input_dir = Path("/dev/input")
    by_path = input_dir / "by-path"

    if by_path.exists():
        for link in by_path.iterdir():
            name = link.name.lower()
            target = link.resolve()
            if "kbd" in name or "keyboard" in name:
                keyboards.append(str(target))
            elif "mouse" in name or "pointer" in name:
                mice.append(str(target))

    # Fallback: try to detect from /proc/bus/input/devices
    if not keyboards or not mice:
        try:
            with open("/proc/bus/input/devices") as f:
                content = f.read()

            current_handlers = []
            is_keyboard = False
            is_mouse = False

            for line in content.split('\n'):
                if line.startswith('N: Name='):
                    name = line.lower()
                    is_keyboard = 'keyboard' in name or 'kbd' in name
                    is_mouse = 'mouse' in name or 'touchpad' in name or 'trackpad' in name
                elif line.startswith('H: Handlers='):
                    handlers = line.split('=')[1].split()
                    for h in handlers:
                        if h.startswith('event'):
                            event_path = f"/dev/input/{h}"
                            if is_keyboard and event_path not in keyboards:
                                keyboards.append(event_path)
                            if is_mouse and event_path not in mice:
                                mice.append(event_path)
                elif line == '':
                    is_keyboard = False
                    is_mouse = False
        except Exception as e:
            print(f"Error reading input devices: {e}", file=sys.stderr)

    # Last resort: use common event devices
    if not keyboards:
        # event3 is often the main keyboard on laptops
        for i in [3, 0, 1, 2]:
            path = f"/dev/input/event{i}"
            if os.path.exists(path):
                keyboards.append(path)
                break

    if not mice:
        # Try to find a mouse device
        for i in range(20):
            path = f"/dev/input/event{i}"
            if os.path.exists(path) and path not in keyboards:
                mice.append(path)
                break

    return keyboards, mice


def update_activity():
    """Update last activity timestamp."""
    now_ms = int(time.time() * 1000)
    try:
        with open(LAST_ACTIVITY_FILE, 'w') as f:
            f.write(str(now_ms))
        with open(IDLE_FILE, 'w') as f:
            f.write("0")
    except Exception:
        pass


def write_counts(keyboard_count, mouse_count):
    """Write current counts to files."""
    try:
        with open(KEYBOARD_COUNT_FILE, 'w') as f:
            f.write(str(keyboard_count))
        with open(MOUSE_COUNT_FILE, 'w') as f:
            f.write(str(mouse_count))
    except Exception as e:
        print(f"Error writing counts: {e}", file=sys.stderr)


def main():
    print("Starting input monitor...", file=sys.stderr)

    keyboards, mice = find_input_devices()
    print(f"Found keyboards: {keyboards}", file=sys.stderr)
    print(f"Found mice: {mice}", file=sys.stderr)

    if not keyboards and not mice:
        print("No input devices found! Make sure you're in the 'input' group.", file=sys.stderr)
        sys.exit(1)

    # Open all devices
    fds = {}
    for dev in keyboards:
        try:
            fd = os.open(dev, os.O_RDONLY | os.O_NONBLOCK)
            fds[fd] = ('keyboard', dev)
            print(f"Opened keyboard: {dev}", file=sys.stderr)
        except PermissionError:
            print(f"Permission denied for {dev}. Add yourself to 'input' group.", file=sys.stderr)
        except Exception as e:
            print(f"Error opening {dev}: {e}", file=sys.stderr)

    for dev in mice:
        try:
            fd = os.open(dev, os.O_RDONLY | os.O_NONBLOCK)
            fds[fd] = ('mouse', dev)
            print(f"Opened mouse: {dev}", file=sys.stderr)
        except PermissionError:
            print(f"Permission denied for {dev}. Add yourself to 'input' group.", file=sys.stderr)
        except Exception as e:
            print(f"Error opening {dev}: {e}", file=sys.stderr)

    if not fds:
        print("Could not open any input devices!", file=sys.stderr)
        sys.exit(1)

    # Initialize counts
    keyboard_count = 0
    mouse_count = 0
    last_write = time.time()

    # Initialize files
    write_counts(0, 0)
    update_activity()

    print("Monitoring input events...", file=sys.stderr)

    try:
        while True:
            # Wait for events with timeout
            readable, _, _ = select.select(list(fds.keys()), [], [], 1.0)

            for fd in readable:
                device_type, device_path = fds[fd]
                try:
                    # Read events
                    while True:
                        try:
                            data = os.read(fd, EVENT_SIZE)
                            if len(data) < EVENT_SIZE:
                                break

                            _, _, ev_type, ev_code, ev_value = struct.unpack(EVENT_FORMAT, data)

                            # Count key presses (not releases)
                            if ev_type == EV_KEY and ev_value == KEY_PRESS:
                                if device_type == 'keyboard':
                                    keyboard_count += 1
                                else:
                                    mouse_count += 1  # Mouse button click
                                update_activity()

                            # Count mouse movements
                            elif ev_type == EV_REL and device_type == 'mouse':
                                mouse_count += 1
                                update_activity()

                            # Count touchpad events
                            elif ev_type == EV_ABS:
                                mouse_count += 1
                                update_activity()

                        except BlockingIOError:
                            break
                        except Exception as e:
                            print(f"Error reading from {device_path}: {e}", file=sys.stderr)
                            break

                except Exception as e:
                    print(f"Error processing {device_path}: {e}", file=sys.stderr)

            # Write counts periodically (every second)
            now = time.time()
            if now - last_write >= 1.0:
                write_counts(keyboard_count, mouse_count)
                last_write = now

    except KeyboardInterrupt:
        print("\nStopping input monitor...", file=sys.stderr)
    finally:
        for fd in fds:
            try:
                os.close(fd)
            except Exception:
                pass


if __name__ == '__main__':
    main()
