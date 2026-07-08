#!/bin/bash
set -euo pipefail

APP="${APP:-$HOME/.local/bin/gnomekiosk-demo}"

my_session() {
    loginctl list-sessions --no-legend 2>/dev/null \
      | awk -v u="$(id -un)" '$3==u {print $1}' \
      | while read -r s; do
            [ "$(loginctl show-session "$s" -p Seat --value)" = "seat0" ] && {
                echo "$s"
                break
            }
        done
}

if [ ! -x "$APP" ]; then
    echo "demo binary not found: $APP" >&2
    exit 1
fi

SID="$(my_session)"

while true; do
    "$APP" &
    app_pid=$!

    while kill -0 "$app_pid" 2>/dev/null; do
        if [ -n "${SID:-}" ] && [ "$(loginctl show-session "$SID" -p Active --value)" != "yes" ]; then
            loginctl activate "$SID" || true
        fi
        sleep 1
    done

    wait "$app_pid" || true
    sleep 1
done
