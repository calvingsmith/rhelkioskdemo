#!/bin/bash
set -euo pipefail

APP="${APP:-$HOME/.local/bin/gnomekiosk-demo}"
REMOTE_ENV="${REMOTE_ENV:-$HOME/.config/gnome-kiosk-demo/remote-access.env}"
REMOTE_STATE_DIR="${REMOTE_STATE_DIR:-$HOME/.local/state/gnome-kiosk-demo}"
REMOTE_LOG="$REMOTE_STATE_DIR/remote-access.log"

if [ -f "$REMOTE_ENV" ]; then
    # shellcheck disable=SC1090
    source "$REMOTE_ENV"
fi

ENABLE_REMOTE_ACCESS="${ENABLE_REMOTE_ACCESS:-1}"
ENABLE_RDP="${ENABLE_RDP:-1}"
ENABLE_VNC="${ENABLE_VNC:-1}"
RDP_USERNAME="${RDP_USERNAME:-kioskusr}"
RDP_PASSWORD="${RDP_PASSWORD:-welcome1}"
VNC_PASSWORD="${VNC_PASSWORD:-welcome1}"
VNC_AUTH_METHOD="${VNC_AUTH_METHOD:-password}"
KEYRING_PASSWORD="${KEYRING_PASSWORD:-welcome1}"

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

setup_remote_access() {
    if [ "$ENABLE_REMOTE_ACCESS" != "1" ]; then
        return 0
    fi

    mkdir -p "$REMOTE_STATE_DIR" "$HOME/.local/share/gnome-remote-desktop"
    : >"$REMOTE_LOG"
    systemctl --user stop gnome-remote-desktop.service >>"$REMOTE_LOG" 2>&1 || true

    if [ "$VNC_AUTH_METHOD" = "password" ] || [ "$ENABLE_RDP" = "1" ]; then
        # Autologin does not unlock the login keyring; unlock it explicitly to avoid GUI prompts.
        if ! printf '%s' "$KEYRING_PASSWORD" | gnome-keyring-daemon --replace --unlock >>"$REMOTE_LOG" 2>&1; then
            if ! printf '' | gnome-keyring-daemon --replace --unlock >>"$REMOTE_LOG" 2>&1; then
                echo "failed to unlock keyring for remote credentials" >>"$REMOTE_LOG"
                return 1
            fi
        fi
    fi

    grdctl_has_vnc=0
    if grdctl --help 2>/dev/null | grep -qE '^[[:space:]]+vnc[[:space:]]'; then
        grdctl_has_vnc=1
    fi

    # Avoid login keyring prompts in kiosk mode: by default do not store secrets for RDP.
    if [ "$ENABLE_RDP" = "1" ]; then
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
    else
        grdctl rdp disable >>"$REMOTE_LOG" 2>&1 || true
        gsettings set org.gnome.desktop.remote-desktop.rdp enable false >>"$REMOTE_LOG" 2>&1 || true
    fi

    if [ "$ENABLE_VNC" = "1" ]; then
        if [ "$grdctl_has_vnc" = "1" ]; then
            grdctl vnc set-auth-method "$VNC_AUTH_METHOD" >>"$REMOTE_LOG" 2>&1
            if [ "$VNC_AUTH_METHOD" = "password" ]; then
                grdctl vnc set-password "$VNC_PASSWORD" >>"$REMOTE_LOG" 2>&1
            fi
            grdctl vnc disable-view-only >>"$REMOTE_LOG" 2>&1
            grdctl vnc enable-port-negotiation >>"$REMOTE_LOG" 2>&1
            grdctl vnc enable >>"$REMOTE_LOG" 2>&1
        else
            echo "grdctl vnc subcommands unavailable; leaving VNC backend unchanged." >>"$REMOTE_LOG"
        fi
        gsettings set org.gnome.desktop.remote-desktop.vnc encryption "['none']" >>"$REMOTE_LOG" 2>&1
    else
        if [ "$grdctl_has_vnc" = "1" ]; then
            grdctl vnc disable >>"$REMOTE_LOG" 2>&1 || true
        fi
    fi

    systemctl --user start gnome-remote-desktop.service >>"$REMOTE_LOG" 2>&1
}

if [ ! -x "$APP" ]; then
    echo "demo binary not found: $APP" >&2
    exit 1
fi

SID="$(my_session)"

if ! setup_remote_access; then
    echo "remote access setup failed; inspect $REMOTE_LOG" >&2
fi

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
