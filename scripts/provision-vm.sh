#!/bin/bash
set -euo pipefail

HOST="${HOST:-192.168.122.79}"
REMOTE_USER="${REMOTE_USER:-ansible}"
KIOSK_USER="${KIOSK_USER:-kioskusr}"
SSH_KEY="${SSH_KEY:-$HOME/.ssh/id_demo}"
APP_NAME="${APP_NAME:-gnomekiosk-demo}"
APP_BIN="${APP_BIN:-$PWD/build/$APP_NAME}"
LOCAL_LAUNCHER="${LOCAL_LAUNCHER:-$PWD/scripts/gnome-kiosk-script.sh}"
ENABLE_REMOTE_ACCESS="${ENABLE_REMOTE_ACCESS:-1}"
ENABLE_RDP="${ENABLE_RDP:-1}"
ENABLE_VNC="${ENABLE_VNC:-1}"
ENABLE_VNC_FANOUT="${ENABLE_VNC_FANOUT:-1}"
VNC_FANOUT_PORTS="${VNC_FANOUT_PORTS:-5901 5902}"
ENABLE_SSH_VNC_TUNNELS="${ENABLE_SSH_VNC_TUNNELS:-1}"
SSH_TUNNEL_USER_A="${SSH_TUNNEL_USER_A:-user}"
SSH_TUNNEL_PASSWORD_A="${SSH_TUNNEL_PASSWORD_A:-user}"
SSH_TUNNEL_PORT_A="${SSH_TUNNEL_PORT_A:-5901}"
SSH_TUNNEL_USER_B="${SSH_TUNNEL_USER_B:-supervisor}"
SSH_TUNNEL_PASSWORD_B="${SSH_TUNNEL_PASSWORD_B:-supervisor}"
SSH_TUNNEL_PORT_B="${SSH_TUNNEL_PORT_B:-5902}"
RDP_USERNAME="${RDP_USERNAME:-kioskusr}"
RDP_PASSWORD="${RDP_PASSWORD:-welcome1}"
VNC_PASSWORD="${VNC_PASSWORD:-welcome1}"
VNC_AUTH_METHOD="${VNC_AUTH_METHOD:-password}"
KEYRING_PASSWORD="${KEYRING_PASSWORD:-welcome1}"
RESET_KEYRING="${RESET_KEYRING:-1}"

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

dnf -y install gnome-kiosk gtk4 dconf gnome-remote-desktop openssl seahorse socat

install -d -o "$KIOSK_USER" -g "$KIOSK_USER" "/home/$KIOSK_USER/.local/bin"
install -m 0755 /tmp/$APP_NAME "/home/$KIOSK_USER/.local/bin/$APP_NAME"
install -m 0755 /tmp/gnome-kiosk-script "/home/$KIOSK_USER/.local/bin/gnome-kiosk-script"
install -d -o "$KIOSK_USER" -g "$KIOSK_USER" "/home/$KIOSK_USER/.config/gnome-kiosk-demo"
install -d -o "$KIOSK_USER" -g "$KIOSK_USER" "/home/$KIOSK_USER/.config/systemd/user/default.target.wants"

# Prevent gnome-remote-desktop from auto-starting before kiosk boot logic runs.
rm -f "/home/$KIOSK_USER/.config/systemd/user/default.target.wants/gnome-remote-desktop.service"
rm -f "/home/$KIOSK_USER/.config/systemd/user/default.target.wants/gnome-remote-desktop-headless.service"

if [ "$RESET_KEYRING" = "1" ]; then
    rm -f "/home/$KIOSK_USER/.local/share/keyrings/login.keyring"
    rm -f "/home/$KIOSK_USER/.local/share/keyrings/user.keystore"
fi

cat >"/home/$KIOSK_USER/.config/gnome-kiosk-demo/remote-access.env" <<CONF
ENABLE_REMOTE_ACCESS='$ENABLE_REMOTE_ACCESS'
ENABLE_RDP='$ENABLE_RDP'
ENABLE_VNC='$ENABLE_VNC'
RDP_USERNAME='$RDP_USERNAME'
RDP_PASSWORD='$RDP_PASSWORD'
VNC_PASSWORD='$VNC_PASSWORD'
VNC_AUTH_METHOD='$VNC_AUTH_METHOD'
KEYRING_PASSWORD='$KEYRING_PASSWORD'
CONF
chown "$KIOSK_USER:$KIOSK_USER" "/home/$KIOSK_USER/.config/gnome-kiosk-demo/remote-access.env"
chmod 0600 "/home/$KIOSK_USER/.config/gnome-kiosk-demo/remote-access.env"

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

cat >/etc/dconf/db/local.d/01-kiosk-remote-desktop <<'CONF'
[org/gnome/desktop/remote-desktop/vnc]
enable=false
auth-method='password'
encryption=['none']
view-only=false

[org/gnome/desktop/remote-desktop/rdp]
enable=false
view-only=false
CONF

dconf update

loginctl enable-linger "$KIOSK_USER"

cat >/etc/systemd/system/gnomekiosk-vnc-fanout@.service <<'CONF'
[Unit]
Description=Kiosk VNC fanout proxy to localhost:5900
After=network-online.target

[Service]
ExecStart=/usr/bin/socat TCP-LISTEN:%i,reuseaddr,fork TCP:127.0.0.1:5900
Restart=always
RestartSec=1
PrivateTmp=true
NoNewPrivileges=true
ProtectSystem=strict
ProtectHome=true
RestrictAddressFamilies=AF_INET AF_INET6 AF_UNIX
RestrictNamespaces=true

[Install]
WantedBy=multi-user.target
CONF

systemctl disable --now gnomekiosk-vnc-fanout@5901.socket 2>/dev/null || true
systemctl disable --now gnomekiosk-vnc-fanout@5902.socket 2>/dev/null || true
rm -f /etc/systemd/system/gnomekiosk-vnc-fanout@.socket
systemctl daemon-reload
if [ "$ENABLE_VNC_FANOUT" = "1" ]; then
    for port in $VNC_FANOUT_PORTS; do
        systemctl enable --now "gnomekiosk-vnc-fanout@\${port}.service"
    done
else
    systemctl disable --now gnomekiosk-vnc-fanout@5901.service 2>/dev/null || true
    systemctl disable --now gnomekiosk-vnc-fanout@5902.service 2>/dev/null || true
fi

if command -v firewall-cmd >/dev/null 2>&1 && systemctl is-active --quiet firewalld; then
    if firewall-cmd --get-services | tr ' ' '\n' | grep -qx 'rdp'; then
        firewall-cmd --permanent --add-service=rdp
    else
        firewall-cmd --permanent --add-port=3389/tcp
    fi

    if firewall-cmd --get-services | tr ' ' '\n' | grep -qx 'vnc'; then
        firewall-cmd --permanent --add-service=vnc
    else
        firewall-cmd --permanent --add-port=5900/tcp
    fi

    if [ "$ENABLE_VNC_FANOUT" = "1" ]; then
        for port in $VNC_FANOUT_PORTS; do
            firewall-cmd --permanent --add-port="\${port}/tcp"
        done
    fi

    firewall-cmd --reload
fi

if [ "$ENABLE_SSH_VNC_TUNNELS" = "1" ]; then
    id -u "$SSH_TUNNEL_USER_A" >/dev/null 2>&1 || useradd -m -s /bin/bash "$SSH_TUNNEL_USER_A"
    id -u "$SSH_TUNNEL_USER_B" >/dev/null 2>&1 || useradd -m -s /bin/bash "$SSH_TUNNEL_USER_B"
    printf '%s:%s\n' "$SSH_TUNNEL_USER_A" "$SSH_TUNNEL_PASSWORD_A" | chpasswd
    printf '%s:%s\n' "$SSH_TUNNEL_USER_B" "$SSH_TUNNEL_PASSWORD_B" | chpasswd

    cat >/etc/ssh/sshd_config.d/90-gnomekiosk-vnc-tunnels.conf <<CONF
Match User $SSH_TUNNEL_USER_A
    PasswordAuthentication yes
    KbdInteractiveAuthentication no
    PubkeyAuthentication no
    PermitTTY no
    X11Forwarding no
    AllowAgentForwarding no
    AllowTcpForwarding local
    PermitOpen 127.0.0.1:$SSH_TUNNEL_PORT_A
    ForceCommand /usr/bin/sleep infinity

Match User $SSH_TUNNEL_USER_B
    PasswordAuthentication yes
    KbdInteractiveAuthentication no
    PubkeyAuthentication no
    PermitTTY no
    X11Forwarding no
    AllowAgentForwarding no
    AllowTcpForwarding local
    PermitOpen 127.0.0.1:$SSH_TUNNEL_PORT_B
    ForceCommand /usr/bin/sleep infinity
CONF
    systemctl reload sshd
else
    rm -f /etc/ssh/sshd_config.d/90-gnomekiosk-vnc-tunnels.conf
    systemctl reload sshd
fi

systemctl set-default graphical.target
systemctl reboot --no-block
EOF
