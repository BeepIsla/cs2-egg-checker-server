#!/usr/bin/env bash
set -e

cd /app

while true; do
    # Check and update protobufs if submodule changed, rebuild if needed
    if [ -f "./update_protobufs.sh" ]; then
        ./update_protobufs.sh --build
    fi

    # Ensure steamclient.so is in place
    if [ -f "/root/.steam/sdk64/linux64/steamclient.so" ] && [ ! -f "/root/.steam/sdk64/steamclient.so" ]; then
        cp /root/.steam/sdk64/linux64/steamclient.so /root/.steam/sdk64/steamclient.so
    fi

    echo "[Startup] Starting CS2 Egg Checker Server..."
    set +e
    /app/build/release/bin/cs2-egg-checker-server "$@"
    EXIT_CODE=$?
    set -e

    echo "[Server] Process exited with code ${EXIT_CODE}."
    if [ "${EXIT_CODE}" -eq 42 ]; then
        echo "[Server] Detected version update / outdated protobufs signal (code 42). Restarting server loop and updating protobufs..."
        sleep 2
    else
        # Normal shutdown or fatal error: exit entrypoint
        exit "${EXIT_CODE}"
    fi
done
