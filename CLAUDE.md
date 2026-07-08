This is a demo project, focused on creating custom software to run in a gnome-kiosk setup.

# architecture

This is a demo that will use a libvirt VM running either RHEL10 or Fedora 44, which will have an implemented kiosk-mode. The designed software must be able to run as part of the local .local/bin/gnome-kiosk-script.

## General setup - Fedora44 server

VM is running as IP 192.168.122.79 - the kiosk is using a dedicated user called kioskusr. This user does have a password (welcome1) which may be used for ssh injection (if easier, a key based auth can be added).  The package "gnome-kiosk" was installed. 

gdm is configured with the following:

```
kioskusr@fedora:~/.local/bin$ cat /etc/gdm/custom.conf
# GDM configuration storage

[daemon]
AutomaticLoginEnable=True
AutomaticLogin=kioskusr
```

This only works because the user was added to the gnome-kiosk DE from GDM.

The current gnome-kiosk-script has a workaround for a timing issue (lack of session management when using gnome-kiosk), making the "simple" script look like:

```
#!/bin/bash

# ( for i in $(seq 1 20); do loginctl activate "$XDG_SESSION_ID" 2>/dev/null; sleep 1; done ) &

# Resolve *my* graphical session on seat0 (don't trust $XDG_SESSION_ID)
my_session() {
    loginctl list-sessions --no-legend 2>/dev/null \
      | awk -v u="$(id -un)" '$3==u {print $1}' \
      | while read -r s; do
            [ "$(loginctl show-session "$s" -p Seat --value)" = "seat0" ] && { echo "$s"; break; }
        done
}

LOG=/home/kioskusr/kiosk-login.log

# Launch Firefox in a continuous keep-alive loop

export LD_LIBRARY_PATH=/usr/lib64
export QT_PLUGIN_PATH=/usr/lib64/qt6/plugins
export QT_QPA_PLATFORM_PLUGIN_PATH=/usr/lib64/qt6/plugins/platforms
# ltrace -C -l 'libQt*' ~/bin/mcp.exe.101 --dev 2>&1 | grep -iE 'window|attribute|show|resize|attribute' > ~/mcp-trace.txt &
~/bin/mcp.exe.101 --dev &

SID="$(my_session)"

while true; do
  if [ "$(loginctl show-session "$SID" -p Active --value)" != "yes" ]; then
      loginctl activate "$SID"
  fi
  sleep .2
done
```

The ~/bin/mcp.exe.101 is the current custom code to which I do not have the source.  This code is based on QT - I'm proposing using GTK instead. See below.

The first decision is to find a way to quickly/easily update the VM with an executable designed here for testing. We could simply use scp to put the executable in place, and reboot the VM.

# Code need for the demo

This is a dummy code. There will be no real live data or anything, instead this is to focus on window management.  One of the key requirements is that the "skin" of the windows must look and feel like old Motif. This means no Gnome-shell, no colorful "Windows like" menus. The current demo has a window with a series of icons that would act as a top bar/top window menu.

The application implements a traditional MDI application which can show/operate "child windows" inside of it. The kiosk mode maximizes the MDI, and no menu or bars from a traditional gnome environment can be seen.

When the application runs, it will be through RDP. Each RDP session will show the SAME live session (part of the challenge here is making sure the application can handle this) but the application must be able to determin which session is interacting with the windows - some windows will have content only a particular user (rdp session) can use/modify.

To make it look realalistic the MDI background should look like a radar image. A cool effect if the green indicator than old radars have would cycle as an animation. The windows will be on the side/top of this.

Another requirement that can be a bit tough with a VM is having multiple monitors. Particular with RDP sessions - advice is needed. One aspect is that a particular window/application output will be pinned to a specific monitor, while another is where the operators keep other windows, type in data etc.

If the demo app can look a bit like flight control windows - meaning the window titles and content would be pretend data of flight paths, requests, tasks for syncing flights both in the air and on the ground. It doesn't have to be, but it would add to the realism.

Key challenge is: Can this be made to work on RHEL10 (right now Fedora44 is the server created for this) and how to setup the system so the above access is possible, which uses as much as possible from a standard Fedora/RHEL box to do window management. Window positioning, minimizing/maximizing the child windows, having a window "stuck" to the top or bottom with status messages (like "always on top" and inmovable). The current code-base contains a lot of window management code - and the idea of this demo is to try to leverage the existing mutter or similar interface through GTK3/GTK4 to do the same.

Preferred programming langauge is C/C++.

The VM is the same architecture as this developer system. So the compilers on this system can be used without setting up remote builds.

## Session decisions

Conclusions from the current design discussion:

- Update/restart flow: deploys are done with a reboot after copying the new binary/configs.
- Provisioning/update path: use an admin SSH account with sudo privileges; keep `kioskusr` runtime-only.
- UI stack: GTK4 on Wayland only.
- Visual style: strict Motif-inspired look and behavior; preserve old-school widget and window-management feel.
- App model: a single MDI-style main application with child windows for now.
- App model update: drop the fake MDI; use separate real GtkWindow toplevels so Mutter handles move/resize/focus/stacking.
- Session model: multiple RDP endpoints/ports with different access roles.
- Remote access baseline: enable both RDP and VNC against the active kiosk desktop so two remote clients can connect to the same live session.
- Remote access implementation note: avoid approval/keyring popups in kiosk mode by unlocking keyring at session start (when needed) and using boot-time credential provisioning only.
- Remote access startup ordering matters: avoid enabling `gnome-remote-desktop.service` persistently for kiosk autologin; start it from kiosk boot logic after keyring/credentials setup.
- Multi-monitor: support an RDP-based multi-monitor demo if feasible.
- Demo data: synthetic flight-control-style data with realistic call signs, locations, and periodic updates.
- Persistence: keep app state and windows alive across RDP disconnects; add a layout reset control for testing.

## Kiosk configuration approach

- The existing kiosk config does not need to be preserved; we can replace it with a clean demo-oriented setup.
- Use an admin SSH account with sudo to provision the VM, copy the binary, install the launcher, and write GDM settings.
- Keep `kioskusr` runtime-only and launch the demo from `~/.local/bin/gnome-kiosk-script`.
- On update, copy the new build and reboot the VM rather than trying to restart the kiosk session in place.
- Provisioning should set GNOME power/session defaults for kiosk reliability (no blanking/sleep/lock) via dconf.
- Window locking for alert-style popups should be handled later with real toplevel window behavior, not in-app MDI widgets.

## Remote access topology options (same live kiosk desktop)

### Option 1: Single shared endpoint (one VNC port, e.g. 5900)

**How it works**
- All operators connect to the same GNOME Remote Desktop VNC endpoint.
- Multiple clients can view/control the same live desktop.

**Pros**
- Simplest and most stable setup in Wayland kiosk.
- Minimal moving parts.
- Closest to stock GNOME/Fedora behavior.

**Cons**
- No per-operator port identity (everyone uses the same endpoint).
- No native per-operator password/role split on the same shared VNC endpoint.
- App-side “which operator clicked” is not directly provided by GTK/Wayland input APIs.

### Option 2: Two inbound ports for testing (e.g. 5901 and 5902) to the same live desktop

**How it works**
- Present two externally visible endpoints for test convenience.
- Intended for local testing where both clients may originate from the same machine.

**Pros**
- Easy to test two connections from one terminal/client host.
- Can tag “entry path” by inbound endpoint in an access layer.

**Cons**
- A plain TCP proxy alone does **not** provide distinct authentication/authorization.
- Different passwords/roles per endpoint require an auth-aware gateway/repeater layer, not just port forwarding.
- Adds operational complexity and more components to harden.

**Current test requirement**
- For this demo/testing workflow, Option 2 is required so two client paths can be exercised from one terminal host.
- The design target is separate credentials/roles per path (not just proxying the same access rights).
- Current implementation provides two ingress ports (5901/5902) that forward to the same live GNOME VNC endpoint (5900) for testing convenience.
- Important limitation: this fanout preserves one backend auth domain; per-port unique VNC passwords/roles are **not** natively possible without an auth-aware gateway layer.

### Option 3: Reintroduce RDP role endpoints later

**How it works**
- Use RDP-specific endpointing/credential strategy for role separation.

**Pros**
- Better alignment with richer auth/role models and enterprise remote workflows.
- Potentially better performance than VNC for dynamic content.

**Cons**
- In kiosk autologin, credential storage can trigger keyring/approval UX issues unless carefully provisioned.
- More integration work needed before it is kiosk-safe and non-interactive at boot.

## Client troubleshooting notes

- If `vncviewer` appears to run but no window/prompt appears, run it in foreground with debug output:
  - `vncviewer -Shared -Log *:stderr:100 192.168.122.79:5901`
- Test alternative endpoint:
  - `vncviewer -Shared 192.168.122.79:5902`
- `remote-viewer` multi-session testing:
  - start separate processes per endpoint (e.g. one process to `:5901`, another to `:5902`)
  - if needed, force separate launches by running them from separate terminals

## SSH tunnel role split (current fast-path for separate credentials)

- Purpose: provide two distinct credentials for two operator paths while still sharing the same live kiosk desktop.
- Provisioning creates two restricted SSH accounts (default `user`, `supervisor`) with distinct passwords.
- Each account is constrained to local TCP forwarding only:
  - `user` -> `127.0.0.1:5901`
  - `supervisor` -> `127.0.0.1:5902`
- This gives distinct authentication identities per operator path now, while backend VNC remains one shared desktop/auth domain.

Example usage:
- Terminal A:
  - `ssh -N -L 15901:127.0.0.1:5901 user@192.168.122.79`
  - then connect VNC client to `127.0.0.1:15901`
- Terminal B:
  - `ssh -N -L 15902:127.0.0.1:5902 supervisor@192.168.122.79`
  - then connect VNC client to `127.0.0.1:15902`