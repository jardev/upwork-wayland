#!/usr/bin/env bash

# Monitor Hyprland workspace changes and cache screenshots for Upwork workspaces
# Also manages input monitoring for activity tracking

SCRIPT_DIR=$(dirname "$(realpath "$0")")
CACHEFILE="/tmp/upwork-cache.png"
IDLE_FILE="/tmp/upwork-idle-ms"
LOG="/tmp/upwork-workspace-monitor.log"
LAST_WORKSPACE=""

echo "Starting workspace monitor at $(date)" >> "$LOG"

# Initialize idle file with 0 (active)
echo "0" > "$IDLE_FILE"

# Start input monitor if not already running
start_input_monitor() {
    if ! pgrep -f "input-monitor.py" > /dev/null; then
        echo "Starting input monitor..." >> "$LOG"
        nohup "$SCRIPT_DIR/input-monitor.py" >> "$LOG" 2>&1 &
        INPUT_MONITOR_PID=$!
        echo "Started input monitor (PID: $INPUT_MONITOR_PID)" >> "$LOG"
    else
        echo "Input monitor already running" >> "$LOG"
    fi
}

# Start the input monitor
start_input_monitor

# Monitor workspace changes
while true; do
    CURRENT_WORKSPACE=$(hyprctl activeworkspace -j 2>/dev/null | jq -r '.id' 2>/dev/null)

    if [ -z "$CURRENT_WORKSPACE" ]; then
        echo "Error getting workspace, sleeping..." >> "$LOG"
        sleep 2
        continue
    fi

    # Only take action if workspace changed
    if [ "$CURRENT_WORKSPACE" != "$LAST_WORKSPACE" ]; then
        echo "Workspace changed from $LAST_WORKSPACE to $CURRENT_WORKSPACE at $(date)" >> "$LOG"
        LAST_WORKSPACE="$CURRENT_WORKSPACE"

        # Reset idle on workspace change (user is clearly active)
        echo "0" > "$IDLE_FILE"

        # Cache screenshot for work workspaces (6, 7, 8, 9)
        if [[ "$CURRENT_WORKSPACE" -ge 6 && "$CURRENT_WORKSPACE" -le 9 ]]; then
            echo "Caching screenshot for workspace $CURRENT_WORKSPACE" >> "$LOG"
            # Small delay to let workspace switch complete
            sleep 0.3
            grim "$CACHEFILE" 2>> "$LOG"
            if [ $? -eq 0 ]; then
                echo "Screenshot cached successfully" >> "$LOG"
                # Also cache window info to match the screenshot
                hyprctl activewindow -j 2>/dev/null | jq -r '.title' > /tmp/upwork-cache-window-name 2>/dev/null
                hyprctl activewindow -j 2>/dev/null | jq -r '.pid' > /tmp/upwork-cache-window-pid 2>/dev/null
                echo "Window info cached" >> "$LOG"
            else
                echo "Failed to cache screenshot" >> "$LOG"
            fi
        fi
    fi

    # Poll every 0.5 seconds
    sleep 0.5
done
