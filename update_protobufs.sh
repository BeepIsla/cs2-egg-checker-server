#!/usr/bin/env bash
set -e

# Change to project root directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

PROTO_DIR="submodules/protobufs"
OUT_DIR="src/protobufs"

echo "[Protobufs] Checking for updates in $PROTO_DIR..."

# Locate protoc compiler
if [ -f "submodules/s2sdk/devtools/bin/linux/protoc" ]; then
    PROTOC="submodules/s2sdk/devtools/bin/linux/protoc"
elif [ -f "submodules/s2sdk/devtools/bin/protoc.exe" ]; then
    PROTOC="submodules/s2sdk/devtools/bin/protoc.exe"
elif command -v protoc >/dev/null 2>&1; then
    PROTOC="protoc"
else
    echo "[Error] protoc compiler not found!"
    exit 1
fi
chmod +x "$PROTOC" 2>/dev/null || true

FORCE=false
if [ "$1" == "--force" ]; then
    FORCE=true
fi

# Check if git is available and proto dir is a valid git repository
if git -C "$PROTO_DIR" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    CURRENT_COMMIT="$(git -C "$PROTO_DIR" rev-parse HEAD 2>/dev/null || echo "")"
    git -C "$PROTO_DIR" fetch origin master --quiet 2>/dev/null || true
    REMOTE_COMMIT="$(git -C "$PROTO_DIR" rev-parse origin/master 2>/dev/null || echo "")"

    if [ "$FORCE" = false ] && [ -n "$CURRENT_COMMIT" ] && [ "$CURRENT_COMMIT" = "$REMOTE_COMMIT" ]; then
        echo "[Protobufs] Protobufs are already at latest commit (${CURRENT_COMMIT:0:7})."
        exit 0
    fi

    if [ -n "$REMOTE_COMMIT" ]; then
        echo "[Protobufs] Updating protobufs: ${CURRENT_COMMIT:0:7} -> ${REMOTE_COMMIT:0:7}"
        git -C "$PROTO_DIR" checkout origin/master --quiet
    fi
else
    if [ ! -f "$PROTO_DIR/csgo/base_gcmessages.proto" ]; then
        echo "[Protobufs] Cloning latest protobufs repo..."
        rm -rf "$PROTO_DIR"
        git clone --depth 1 https://github.com/SteamTracking/Protobufs.git "$PROTO_DIR"
    else
        echo "[Protobufs] Pre-packaged proto files found in $PROTO_DIR."
        if [ "$FORCE" = false ]; then
            exit 0
        fi
    fi
fi

mkdir -p "$OUT_DIR"

echo "[Protobufs] Compiling .proto files with $PROTOC..."
"$PROTOC" \
  --proto_path="./$PROTO_DIR/csgo" \
  --cpp_out="./$OUT_DIR" \
  valveextensions.proto \
  source2_steam_stats.proto \
  connectionless_netmessages.proto \
  steammessages.proto \
  netmessages.proto \
  usermessages.proto \
  network_connection.proto \
  networkbasetypes.proto \
  base_gcmessages.proto \
  engine_gcmessages.proto \
  gcsystemmsgs.proto \
  gcsdk_gcmessages.proto \
  cstrike15_gcmessages.proto 2>&1 | grep -v "No syntax specified" || true

echo "[Protobufs] Successfully generated protobuf files in $OUT_DIR."

# Rebuild if requested
if [ "$1" == "--build" ] || [ "$2" == "--build" ]; then
    if command -v cmake >/dev/null 2>&1; then
        echo "[Protobufs] Rebuilding release binary..."
        cmake --preset release
        cmake --build --preset release
        echo "[Protobufs] Build complete!"
    else
        echo "[Protobufs] CMake not found; skipped rebuilding binary."
    fi
fi
