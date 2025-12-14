#!/usr/bin/env bash

# This script takes a screenshot and caches it for Upwork
# It should be called when switching TO workspaces 8, 9, or 0

WORKSPACE_ID="$1"
CACHEFILE="/tmp/upwork-cache.png"

# Only cache for workspaces 8, 9, and 10 (10 is the "0" key)
if [[ "$WORKSPACE_ID" == "8" || "$WORKSPACE_ID" == "9" || "$WORKSPACE_ID" == "10" ]]; then
    echo "Caching screenshot for workspace $WORKSPACE_ID"
    grim "$CACHEFILE"
    echo "Screenshot cached to $CACHEFILE"
fi
