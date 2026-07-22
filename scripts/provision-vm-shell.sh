#!/bin/bash
set -euo pipefail

# Provisions the full-gnome-shell kiosk variant. This is intentionally a
# SEPARATE script from scripts/provision-vm.sh (the gnome-kiosk path,
# which stays untouched) rather than a mode switch in it, so the working
# gnome-kiosk provisioning can never be put at risk by changes here. See
# CLAUDE.md "Window minimize" / the gnome-shell menu-bar section.
#
# Differences from provision-vm.sh:
#   - Session: standard "gnome-wayland" (full GNOME Shell), not gnome-kiosk.
#   - Binary: build/gnomekiosk-demo-shell (built via `make shell`), not
#     build/gnomekiosk-demo.
#   - The "Kiosk Layout Helper" extension is installed and enabled.
#   - No custom kiosk launcher script: the app and the remote-access setup
#     each run as a systemd --user service instead, since a normal
#     gnome-session has no equivalent hook to a custom Session= script.
#   - Lockdown is applied via dconf (top bar hidden by the extension
#     itself; logout/lock-screen/user-switching/etc. via
#     org.gnome.desktop.lockdown) instead of gnome-kiosk's inherent
#     chrome-less design.
#   - VNC is not offered here: already confirmed non-functional for
#     concurrent viewing (see CLAUDE.md "SSH tunnel role split"). RDP only.

HOST="${HOST:?Set HOST to the target VM IP, e.g. HOST=192.168.122.111}"
REMOTE_USER="${REMOTE_USER:-ansible}"
KIOSK_USER="${KIOSK_USER:-kioskusr}"
KIOSK_PASSWORD="${KIOSK_PASSWORD:-welcome1}"
SSH_KEY="${SSH_KEY:-$HOME/.ssh/id_demo}"
APP_NAME="${APP_NAME:-gnomekiosk-demo-shell}"
APP_BIN="${APP_BIN:-$PWD/build/$APP_NAME}"
EXTENSION_UUID="kiosk-layout-helper@gnomekiosk.demo"
EXTENSION_DIR="${EXTENSION_DIR:-$PWD/gnome-shell-extension/$EXTENSION_UUID}"
RDP_USERNAME="${RDP_USERNAME:-kioskusr}"
RDP_PASSWORD="${RDP_PASSWORD:-welcome1}"
KEYRING_PASSWORD="${KEYRING_PASSWORD:-welcome1}"

if [ ! -x "$SSH_KEY" ] && [ ! -f "$SSH_KEY" ]; then
    echo "SSH key not found: $SSH_KEY" >&2
    exit 1
fi

if [ ! -x "$APP_BIN" ]; then
    echo "Build the gnome-shell app first: make shell (expects $APP_BIN)" >&2
    exit 1
fi

if [ ! -d "$EXTENSION_DIR" ]; then
    echo "Extension directory not found: $EXTENSION_DIR" >&2
    exit 1
fi

SSH_OPTS=(-i "$SSH_KEY" -o StrictHostKeyChecking=accept-new)

# `scp -r` into a destination that already exists nests the source directory
# one level deeper instead of replacing its contents (classic cp/scp -r
# gotcha) — on a second+ run this silently left the OLD extension.js in
# place at the path GNOME actually loads, while the new one sat unused in
# a nested subdirectory. Remove the remote staging dir first so every run
# gets a clean, flat copy.
ssh "${SSH_OPTS[@]}" "$REMOTE_USER@$HOST" "rm -rf /tmp/$EXTENSION_UUID"
scp "${SSH_OPTS[@]}" "$APP_BIN" "$REMOTE_USER@$HOST:/tmp/$APP_NAME"
scp "${SSH_OPTS[@]}" -r "$EXTENSION_DIR" "$REMOTE_USER@$HOST:/tmp/$EXTENSION_UUID"

ssh -tt "${SSH_OPTS[@]}" "$REMOTE_USER@$HOST" "sudo -n bash -s" <<EOF
set -euo pipefail

dnf -y install gnome-shell gdm gtk4 dconf gnome-remote-desktop openssl seahorse

if ! id -u "$KIOSK_USER" >/dev/null 2>&1; then
    useradd -m -s /bin/bash "$KIOSK_USER"
    printf '%s:%s\n' "$KIOSK_USER" "$KIOSK_PASSWORD" | chpasswd
fi

install -d /var/lib/AccountsService/users
cat >"/var/lib/AccountsService/users/$KIOSK_USER" <<CONF
[User]
Session=gnome-wayland
SystemAccount=false
CONF

install -d -o "$KIOSK_USER" -g "$KIOSK_USER" "/home/$KIOSK_USER/.local"
install -d -o "$KIOSK_USER" -g "$KIOSK_USER" "/home/$KIOSK_USER/.config"
install -d -o "$KIOSK_USER" -g "$KIOSK_USER" "/home/$KIOSK_USER/.local/bin"
install -d -o "$KIOSK_USER" -g "$KIOSK_USER" "/home/$KIOSK_USER/.local/share"
install -d -o "$KIOSK_USER" -g "$KIOSK_USER" "/home/$KIOSK_USER/.local/share/gnome-shell"
install -d -o "$KIOSK_USER" -g "$KIOSK_USER" "/home/$KIOSK_USER/.local/share/gnome-shell/extensions"
install -d -o "$KIOSK_USER" -g "$KIOSK_USER" "/home/$KIOSK_USER/.config/gnome-kiosk-demo"
install -d -o "$KIOSK_USER" -g "$KIOSK_USER" "/home/$KIOSK_USER/.config/systemd/user"
install -d -o "$KIOSK_USER" -g "$KIOSK_USER" "/home/$KIOSK_USER/.config/systemd/user/graphical-session.target.wants"

install -m 0755 "/tmp/$APP_NAME" "/home/$KIOSK_USER/.local/bin/$APP_NAME"

rm -rf "/home/$KIOSK_USER/.local/share/gnome-shell/extensions/$EXTENSION_UUID"
cp -r "/tmp/$EXTENSION_UUID" "/home/$KIOSK_USER/.local/share/gnome-shell/extensions/$EXTENSION_UUID"
chown -R "$KIOSK_USER:$KIOSK_USER" "/home/$KIOSK_USER/.local/share/gnome-shell"

cat >"/home/$KIOSK_USER/.config/gnome-kiosk-demo/remote-access.env" <<CONF
RDP_USERNAME='$RDP_USERNAME'
RDP_PASSWORD='$RDP_PASSWORD'
KEYRING_PASSWORD='$KEYRING_PASSWORD'
CONF
chown "$KIOSK_USER:$KIOSK_USER" "/home/$KIOSK_USER/.config/gnome-kiosk-demo/remote-access.env"
chmod 0600 "/home/$KIOSK_USER/.config/gnome-kiosk-demo/remote-access.env"

# Remote-access setup: same TLS-cert/credentials/keyring-unlock pattern as
# the gnome-kiosk launcher's setup_remote_access(), just run as a oneshot
# systemd --user unit instead of inline in a custom Session= script, since
# a normal gnome-session has no equivalent hook to run it from directly.
cat >"/home/$KIOSK_USER/.local/bin/gnomekiosk-remote-access-setup.sh" <<'SCRIPT'
#!/bin/bash
set -euo pipefail

REMOTE_ENV="\$HOME/.config/gnome-kiosk-demo/remote-access.env"
REMOTE_STATE_DIR="\$HOME/.local/state/gnome-kiosk-demo"
REMOTE_LOG="\$REMOTE_STATE_DIR/remote-access.log"

if [ -f "\$REMOTE_ENV" ]; then
    # shellcheck disable=SC1090
    source "\$REMOTE_ENV"
fi

RDP_USERNAME="\${RDP_USERNAME:-kioskusr}"
RDP_PASSWORD="\${RDP_PASSWORD:-welcome1}"
KEYRING_PASSWORD="\${KEYRING_PASSWORD:-welcome1}"

mkdir -p "\$REMOTE_STATE_DIR" "\$HOME/.local/share/gnome-remote-desktop"
: >"\$REMOTE_LOG"
systemctl --user stop gnome-remote-desktop.service >>"\$REMOTE_LOG" 2>&1 || true

# Autologin does not unlock the login keyring; unlock it explicitly to
# avoid GUI prompts when RDP needs the stored TLS key/credentials.
if ! printf '%s' "\$KEYRING_PASSWORD" | gnome-keyring-daemon --replace --unlock >>"\$REMOTE_LOG" 2>&1; then
    if ! printf '' | gnome-keyring-daemon --replace --unlock >>"\$REMOTE_LOG" 2>&1; then
        echo "failed to unlock keyring for remote credentials" >>"\$REMOTE_LOG"
        exit 1
    fi
fi

if [ ! -s "\$HOME/.local/share/gnome-remote-desktop/tls.key" ] || [ ! -s "\$HOME/.local/share/gnome-remote-desktop/tls.crt" ]; then
    openssl req -new -newkey rsa:2048 -nodes -x509 \
        -subj "/CN=gnomekiosk-demo" \
        -days 3650 \
        -keyout "\$HOME/.local/share/gnome-remote-desktop/tls.key" \
        -out "\$HOME/.local/share/gnome-remote-desktop/tls.crt" >/dev/null 2>&1
    chmod 0600 "\$HOME/.local/share/gnome-remote-desktop/tls.key"
fi

grdctl rdp set-tls-key "\$HOME/.local/share/gnome-remote-desktop/tls.key" >>"\$REMOTE_LOG" 2>&1
grdctl rdp set-tls-cert "\$HOME/.local/share/gnome-remote-desktop/tls.crt" >>"\$REMOTE_LOG" 2>&1
grdctl rdp set-credentials "\$RDP_USERNAME" "\$RDP_PASSWORD" >>"\$REMOTE_LOG" 2>&1
grdctl rdp disable-view-only >>"\$REMOTE_LOG" 2>&1
grdctl rdp disable-port-negotiation >>"\$REMOTE_LOG" 2>&1
if ! grdctl rdp enable >>"\$REMOTE_LOG" 2>&1; then
    gsettings set org.gnome.desktop.remote-desktop.rdp enable true >>"\$REMOTE_LOG" 2>&1 || true
fi

systemctl --user start gnome-remote-desktop.service >>"\$REMOTE_LOG" 2>&1
SCRIPT
chmod 0755 "/home/$KIOSK_USER/.local/bin/gnomekiosk-remote-access-setup.sh"
chown "$KIOSK_USER:$KIOSK_USER" "/home/$KIOSK_USER/.local/bin/gnomekiosk-remote-access-setup.sh"

cat >"/home/$KIOSK_USER/.config/systemd/user/gnomekiosk-remote-access-setup.service" <<'UNIT'
[Unit]
Description=Kiosk demo remote-access setup (TLS/credentials/keyring, then start RDP)
After=graphical-session.target

[Service]
Type=oneshot
ExecStart=%h/.local/bin/gnomekiosk-remote-access-setup.sh
RemainAfterExit=yes

[Install]
WantedBy=graphical-session.target
UNIT

cat >"/home/$KIOSK_USER/.config/systemd/user/$APP_NAME.service" <<UNIT
[Unit]
Description=Kiosk demo app (gnome-shell build)
After=graphical-session.target gnomekiosk-remote-access-setup.service
Wants=gnomekiosk-remote-access-setup.service

[Service]
ExecStart=%h/.local/bin/$APP_NAME
Restart=always
RestartSec=1

[Install]
WantedBy=graphical-session.target
UNIT

ln -sf "../gnomekiosk-remote-access-setup.service" \
    "/home/$KIOSK_USER/.config/systemd/user/graphical-session.target.wants/gnomekiosk-remote-access-setup.service"
ln -sf "../$APP_NAME.service" \
    "/home/$KIOSK_USER/.config/systemd/user/graphical-session.target.wants/$APP_NAME.service"
chown -R "$KIOSK_USER:$KIOSK_USER" "/home/$KIOSK_USER/.config/systemd"

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

# Lockdown: verified against this VM's actual schemas before writing
# (gsettings list-keys org.gnome.desktop.lockdown / .interface / .mutter /
# .shell / .notifications) rather than assumed. The top bar itself is
# hidden by the extension (Main.panel.hide() in enable()), not here.
cat >/etc/dconf/db/local.d/01-kiosk-lockdown <<CONF
[org/gnome/desktop/lockdown]
disable-log-out=true
disable-lock-screen=true
disable-user-switching=true
disable-command-line=true
user-administration-disabled=true

[org/gnome/desktop/notifications]
show-banners=false

[org/gnome/desktop/interface]
enable-hot-corners=false

[org/gnome/mutter]
overlay-key=''
center-new-windows=false

[org/gnome/shell]
enabled-extensions=['$EXTENSION_UUID']
disable-user-extensions=false
CONF

dconf update

loginctl enable-linger "$KIOSK_USER"

if command -v firewall-cmd >/dev/null 2>&1 && systemctl is-active --quiet firewalld; then
    if firewall-cmd --get-services | tr ' ' '\n' | grep -qx 'rdp'; then
        firewall-cmd --permanent --add-service=rdp
    else
        firewall-cmd --permanent --add-port=3389/tcp
    fi
    firewall-cmd --reload
fi

systemctl set-default graphical.target
systemctl reboot --no-block
EOF
