#!/bin/bash
set -euo pipefail

HOST="${HOST:-192.168.122.79}"
REMOTE_USER="${REMOTE_USER:-ansible}"
KIOSK_USER="${KIOSK_USER:-kioskusr}"
SSH_KEY="${SSH_KEY:-$HOME/.ssh/id_demo}"
APP_NAME="${APP_NAME:-gnomekiosk-demo}"
APP_BIN="${APP_BIN:-$PWD/$APP_NAME}"
LOCAL_LAUNCHER="${LOCAL_LAUNCHER:-$PWD/scripts/gnome-kiosk-script.sh}"

if [ ! -x "$SSH_KEY" ] && [ ! -f "$SSH_KEY" ]; then
    echo "SSH key not found: $SSH_KEY" >&2
    exit 1
fi

if [ ! -x "$APP_BIN" ]; then
    echo "Build the app first: $APP_BIN" >&2
    exit 1
fi

if [ ! -f "$LOCAL_LAUNCHER" ]; then
    echo "Launcher script not found: $LOCAL_LAUNCHER" >&2
    exit 1
fi

SSH_OPTS=(-i "$SSH_KEY" -o StrictHostKeyChecking=accept-new)

scp "${SSH_OPTS[@]}" "$APP_BIN" "$REMOTE_USER@$HOST:/tmp/$APP_NAME"
scp "${SSH_OPTS[@]}" "$LOCAL_LAUNCHER" "$REMOTE_USER@$HOST:/tmp/gnome-kiosk-script"

ssh -tt "${SSH_OPTS[@]}" "$REMOTE_USER@$HOST" "sudo -n bash -s" <<EOF
set -euo pipefail

dnf -y install gnome-kiosk gtk4 dconf

install -d -o "$KIOSK_USER" -g "$KIOSK_USER" "/home/$KIOSK_USER/.local/bin"
install -m 0755 /tmp/$APP_NAME "/home/$KIOSK_USER/.local/bin/$APP_NAME"
install -m 0755 /tmp/gnome-kiosk-script "/home/$KIOSK_USER/.local/bin/gnome-kiosk-script"

cat >/etc/gdm/custom.conf <<CONF
# GDM configuration storage

[daemon]
AutomaticLoginEnable=True
AutomaticLogin=$KIOSK_USER
CONF

install -d /etc/dconf/db/local.d
cat >/etc/dconf/db/local.d/00-kiosk-power <<'CONF'
[org/gnome/desktop/session]
idle-delay=uint32 0

[org/gnome/desktop/screensaver]
lock-enabled=false

[org/gnome/settings-daemon/plugins/power]
sleep-inactive-ac-type='nothing'
sleep-inactive-ac-timeout=0
sleep-inactive-battery-type='nothing'
sleep-inactive-battery-timeout=0
power-button-action='nothing'
CONF

dconf update

systemctl set-default graphical.target
systemctl reboot --no-block
EOF
