#!/usr/bin/env bash

# Monitor Hyprland workspace changes and cache screenshots for Upwork workspaces

CACHEFILE="/tmp/upwork-cache.png"
LOG="/tmp/upwork-workspace-monitor.log"
LAST_WORKSPACE=""

echo "Starting workspace monitor at $(date)" >> "$LOG"

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

        # Cache screenshot for work workspaces (8, 9, 10)
        if [[ "$CURRENT_WORKSPACE" == "8" || "$CURRENT_WORKSPACE" == "9" || "$CURRENT_WORKSPACE" == "10" ]]; then
            echo "Caching screenshot for workspace $CURRENT_WORKSPACE" >> "$LOG"
            # Small delay to let workspace switch complete
            sleep 0.3
            grim "$CACHEFILE" 2>> "$LOG"
            if [ $? -eq 0 ]; then
                echo "Screenshot cached successfully" >> "$LOG"
            else
                echo "Failed to cache screenshot" >> "$LOG"
            fi
        fi
    fi

    # Poll every 0.5 seconds
    sleep 0.5
done
