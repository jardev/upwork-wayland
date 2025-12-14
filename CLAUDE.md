# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This project enables Upwork screenshot functionality under Wayland by intercepting and replacing GDK API calls. It's a shared library that uses LD_PRELOAD to inject custom implementations of GDK and X11 functions, allowing the Upwork desktop client (which expects X11) to work on Wayland compositors.

## Build Commands

```bash
# Build the shared library
make

# Clean build artifacts
make clean

# Build test executable (for testing screenshot functionality)
make test
```

Dependencies required:
- gcc
- glib
- gdk-pixbuf
- libx11 (X11 and XScreenSaver extensions)
- pkg-config
- flameshot (or another screenshot tool, configurable)

For Nix users: `nix-shell` will set up the development environment.

## Running the Application

```bash
# Launch Upwork with the wrapper
./upwork.sh

# Specify custom Upwork binary location
UPWORK=/path/to/upwork ./upwork.sh
```

## Architecture

### Core Mechanism: LD_PRELOAD Interception

The project works by preloading `gdk-screenshotter.so` which intercepts specific function calls:

1. **`gdk_pixbuf_get_from_window()`** - Overridden in [gdk-screenshotter.c:49-68](gdk-screenshotter.c#L49-L68)
   - Instead of capturing from GDK window, it forks a process to run a screenshot tool (default: flameshot)
   - Saves screenshot to `/tmp/upwork.png`
   - Loads the saved image as a GdkPixbuf and returns it
   - Uses `WAYLAND_DISPLAY_REAL` environment variable to restore Wayland context in the forked process
   - Configurable via `UPWORK_SCREENSHOT_COMMAND` environment variable

2. **`gdk_pixbuf_save_to_callback()`** - Overridden in [gdk-screenshotter.c:73-94](gdk-screenshotter.c#L73-L94)
   - Proxies to the real implementation using `dlsym(RTLD_NEXT, ...)`
   - Sets `wanna_break_dimensions` flag to trigger dimension spoofing
   - This prevents Upwork from taking a second screenshot via Electron methods

3. **`XGetWindowAttributes()`** - Overridden in [gdk-screenshotter.c:98-131](gdk-screenshotter.c#L98-L131)
   - When `wanna_break_dimensions` is set, returns width=0 and height=0
   - This forces Upwork's Electron code to use the native screenshot instead of retaking it
   - Critical for ensuring the flameshot screenshot is used rather than a broken Electron screenshot

4. **`XGetWindowProperty()`** - Overridden in [gdk-screenshotter.c:164-206](gdk-screenshotter.c#L164-L206)
   - Intercepts requests for `_NET_WM_NAME`, `_NET_WM_PID`, and `_NET_ACTIVE_WINDOW`
   - Uses `swaymsg` (Sway-specific) to query the active window name and PID
   - Returns spoofed window properties to make Upwork think it's running under X11

### Environment Variable Dance

The [upwork.sh](upwork.sh) wrapper script performs crucial environment setup:

- Sets `XDG_SESSION_TYPE=x11` to convince Upwork it's running on X11
- Saves `WAYLAND_DISPLAY` to `WAYLAND_DISPLAY_REAL` before clearing it
- Clears `WAYLAND_DISPLAY` to force Upwork into X11 mode
- Sets `LD_PRELOAD` to inject the shared library
- The shared library restores `WAYLAND_DISPLAY` when forking to run the screenshot tool

### Screenshot Command Customization

Edit line 20 in [upwork.sh](upwork.sh) to change the screenshot tool:

```bash
export UPWORK_SCREENSHOT_COMMAND="flameshot full -p"
```

The command must accept a file path as the next argument. Examples:
- `flameshot full -p` (default)
- `gnome-screenshot -f`
- Any tool that outputs to a path specified as the next argument

## Key Implementation Details

- **Temporary file**: Screenshots are saved to `/tmp/upwork.png` (hardcoded in [gdk-screenshotter.c:13](gdk-screenshotter.c#L13))
- **Shell detection**: Uses `$SHELL` environment variable to execute the screenshot command [gdk-screenshotter.c:36](gdk-screenshotter.c#L36)
- **Sway dependency**: Window name/PID detection currently uses `swaymsg` with `jq` [gdk-screenshotter.c:134-160](gdk-screenshotter.c#L134-L160) - may need adaptation for other Wayland compositors
- **Function interception pattern**: All overridden functions use `dlsym(RTLD_NEXT, ...)` to get the original implementation
- **Debugging**: Extensive printf debugging throughout; can enable LOG4JS_CONFIG in upwork.sh for additional logging

## Wayland Compositor Compatibility

The window property detection code is currently Sway-specific (uses `swaymsg`). For other compositors (GNOME/Mutter, KDE/KWin, etc.), the `get_active_window_name()` and `get_active_window_pid()` functions in [gdk-screenshotter.c:133-160](gdk-screenshotter.c#L133-L160) would need to be adapted to use compositor-specific tools or protocols.
