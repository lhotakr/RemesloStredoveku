#!/usr/bin/env sh
set -eu

cd "$(dirname "$0")/cmake-build-debug"

APP="./RemesloStredoveku"
LIB_PATH="/usr/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu/pulseaudio:/app/lib/x86_64-linux-gnu:/app/lib/x86_64-linux-gnu/pulseaudio:/run/host/usr/lib64:/run/host/usr/lib64/pulseaudio:/run/host/lib64"

if [ ! -x "$APP" ]; then
    echo "Nenalezen spustitelny soubor: cmake-build-debug/RemesloStredoveku" >&2
    echo "Nejdriv projekt sestav v CLionu nebo pres: cmake --build --preset linux-debug" >&2
    exit 1
fi

export LD_LIBRARY_PATH="$LIB_PATH${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

if [ -n "${container:-}" ]; then
    export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-pulseaudio}"
fi

USER_ID="$(id -u)"
PULSE_SOCKET=""
if [ -n "${XDG_RUNTIME_DIR:-}" ] && [ -S "$XDG_RUNTIME_DIR/pulse/native" ]; then
    PULSE_SOCKET="$XDG_RUNTIME_DIR/pulse/native"
elif [ -S "/run/user/$USER_ID/pulse/native" ]; then
    PULSE_SOCKET="/run/user/$USER_ID/pulse/native"
fi

if [ -n "$PULSE_SOCKET" ]; then
    export PULSE_SERVER="${PULSE_SERVER:-unix:$PULSE_SOCKET}"
elif [ -n "${container:-}" ]; then
    echo "Audio socket $XDG_RUNTIME_DIR/pulse/native neni ve Flatpaku dostupny." >&2
    echo "Povol CLionu PulseAudio socket, restartuj CLion/Flatpak a spust skript znovu:" >&2
    echo "  flatpak override --user --socket=pulseaudio com.jetbrains.CLion" >&2
fi

# Stary build muze mit RPATH do /run/host. Ve Flatpak runtime ho obejdeme,
# jinak se SDL audio knihovny misi mezi hostem a sandboxem.
if [ -n "${container:-}" ] && [ -x /lib64/ld-linux-x86-64.so.2 ]; then
    exec /lib64/ld-linux-x86-64.so.2 \
        --inhibit-rpath "$APP" \
        --library-path "$LD_LIBRARY_PATH" \
        "$APP" "$@"
fi

exec "$APP" "$@"
