#!/bin/bash
set -euo pipefail

REMOTE_ENV="$HOME/.config/gnome-kiosk-demo/remote-access.env"
REMOTE_STATE_DIR="$HOME/.local/state/gnome-kiosk-demo"
REMOTE_LOG="$REMOTE_STATE_DIR/remote-access.log"

if [ -f "$REMOTE_ENV" ]; then
    # shellcheck disable=SC1090
    source "$REMOTE_ENV"
fi

RDP_USERNAME="${RDP_USERNAME:-kioskusr}"
RDP_PASSWORD="${RDP_PASSWORD:-welcome1}"
KEYRING_PASSWORD="${KEYRING_PASSWORD:-welcome1}"

mkdir -p "$REMOTE_STATE_DIR" "$HOME/.local/share/gnome-remote-desktop"
: >"$REMOTE_LOG"
systemctl --user stop gnome-remote-desktop.service >>"$REMOTE_LOG" 2>&1 || true

# Autologin does not unlock the login keyring; unlock it explicitly to
# avoid GUI prompts when RDP needs the stored TLS key/credentials.
if ! printf '%s' "$KEYRING_PASSWORD" | gnome-keyring-daemon --replace --unlock >>"$REMOTE_LOG" 2>&1; then
    if ! printf '' | gnome-keyring-daemon --replace --unlock >>"$REMOTE_LOG" 2>&1; then
        echo "failed to unlock keyring for remote credentials" >>"$REMOTE_LOG"
        exit 1
    fi
fi

if [ ! -s "$HOME/.local/share/gnome-remote-desktop/tls.key" ] || [ ! -s "$HOME/.local/share/gnome-remote-desktop/tls.crt" ]; then
    openssl req -new -newkey rsa:2048 -nodes -x509 \
        -subj "/CN=gnomekiosk-demo" \
        -days 3650 \
        -keyout "$HOME/.local/share/gnome-remote-desktop/tls.key" \
        -out "$HOME/.local/share/gnome-remote-desktop/tls.crt" >/dev/null 2>&1
    chmod 0600 "$HOME/.local/share/gnome-remote-desktop/tls.key"
fi

grdctl rdp set-tls-key "$HOME/.local/share/gnome-remote-desktop/tls.key" >>"$REMOTE_LOG" 2>&1
grdctl rdp set-tls-cert "$HOME/.local/share/gnome-remote-desktop/tls.crt" >>"$REMOTE_LOG" 2>&1
grdctl rdp set-credentials "$RDP_USERNAME" "$RDP_PASSWORD" >>"$REMOTE_LOG" 2>&1
grdctl rdp disable-view-only >>"$REMOTE_LOG" 2>&1
grdctl rdp disable-port-negotiation >>"$REMOTE_LOG" 2>&1
if ! grdctl rdp enable >>"$REMOTE_LOG" 2>&1; then
    gsettings set org.gnome.desktop.remote-desktop.rdp enable true >>"$REMOTE_LOG" 2>&1 || true
fi

systemctl --user start gnome-remote-desktop.service >>"$REMOTE_LOG" 2>&1
