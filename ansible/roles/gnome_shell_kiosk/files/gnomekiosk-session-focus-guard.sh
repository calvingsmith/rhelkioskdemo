#!/bin/bash
# Ported from the gnome-kiosk launcher's focus-reclaim loop
# (scripts/gnome-kiosk-script.sh) — GDM's greeter can win a readiness-timeout
# race against gnome-session and take the seat back; this fights to reclaim
# it. Runs for the life of the session (systemd Restart=always), not just at
# boot, since the same race can in principle recur.
#
# Must stand down during shutdown/reboot: the session going inactive is
# normal once logind starts tearing it down, and fighting that by calling
# `loginctl activate` mid-teardown was observed to stall session-2.scope's
# stop job (system shutdown hanging). `timeout` bounds each activate call in
# case logind is unresponsive mid-teardown, and the is-system-running check
# stops new attempts once shutdown begins.
set -uo pipefail
trap 'exit 0' TERM INT

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

SID=""
while [ -z "$SID" ]; do
    SID="$(my_session)"
    [ -z "$SID" ] && sleep 1
done

while true; do
    state="$(systemctl is-system-running 2>/dev/null || true)"
    if [ "$state" = "stopping" ]; then
        exit 0
    fi
    if [ "$(loginctl show-session "$SID" -p Active --value)" != "yes" ]; then
        timeout 3 loginctl activate "$SID" || true
    fi
    sleep 1
done
