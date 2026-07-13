This is a demo project, focused on creating custom software to run in a gnome-kiosk setup.

# architecture

This is a demo that will use a libvirt VM running either RHEL10 or Fedora 44, which will have an implemented kiosk-mode. The designed software must be able to run as part of the local .local/bin/gnome-kiosk-script.

## VM inventory

Both VMs run on the same local libvirt hypervisor, share the `ansible` admin account with the same `~/.ssh/id_demo` key and NOPASSWD sudo, and are provisioned/updated with the same `scripts/provision-vm.sh` (pass a different `HOST`).

- Fedora 44: `192.168.122.79` — see "General setup - Fedora44 server" below.
- RHEL10: `192.168.122.81` — same admin/provisioning setup as Fedora44; already has the kiosk-mode app running via `provision-vm.sh`, same as the Fedora box.

As of 2026-07-09, testing is being focused on the RHEL10 VM (`192.168.122.81`).

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
- Remote access standard path: use GNOME Remote Desktop (RDP backend) and validate with `xfreerdp` as the primary client.
- Multi-monitor: support an RDP-based multi-monitor demo if feasible.
- Demo data: synthetic flight-control-style data with realistic call signs, locations, and periodic updates.
- Persistence: keep app state and windows alive across RDP disconnects; add a layout reset control for testing.

## Layout persistence behavior (GTK4/Wayland)

- We changed layout save/load to persist **live window size/state** (not only default size), so SAVE/LOAD now restores dimensions and minimize/maximize/visibility more reliably.
- Why this change was needed:
  - Previous implementation saved `gtk_window_get_default_size()` values, which are initial defaults and do not reliably match user-resized live geometry.
  - This caused SAVE/LOAD to appear ineffective after manual resize/rearrange actions.
- Current platform limitation (important):
  - On GTK4 + Wayland, toplevel position is controlled by the compositor (Mutter).
  - Applications generally cannot freely read/set absolute X/Y coordinates for normal toplevel windows.
  - As a result, exact coordinate restoration (old X11-style “put this window at x,y”) is not guaranteed/available in the current model.
- Practical outcome:
  - Size + state persistence is supported.
  - Absolute position persistence is not currently supported with plain GTK4 toplevel APIs on Wayland kiosk.

## Deterministic placement plan (next phase)

To get predictable startup placement on RHEL10 while staying on supported components, evaluate in this order:

1. **Compositor-managed strategy first**: rely on Mutter placement rules/behavior and controlled map order for repeatable startup layouts.
2. **If strict coordinates are required**: use an in-app managed workspace model (single primary surface that arranges child panels internally), trading off some native toplevel behavior.
3. Keep RDP baseline unchanged (`gnome-remote-desktop` + `xfreerdp`) during this validation so remote behavior remains stable while layout strategy is tested.

### Decision: compositor-managed placement adopted (2026-07-09)

- Chose option 1 (compositor-managed). Mutter's `center-new-windows` defaults to `true`, which stacks every demo child window exactly on top of each other at map time — not useful for a default layout. `provision-vm.sh` now ships `/etc/dconf/db/local.d/02-kiosk-window-placement` setting `org/gnome/mutter center-new-windows=false`, so Mutter falls back to its normal deterministic cascade placement instead.
- The app's window map order is already fixed (`WIN_RADAR` first and maximized, then `TRAFFIC`, `CLEARANCE`, `GROUND`, `STATUS` in that order in `show_default_windows()`), so RESET/LOAD/boot all cascade windows the same way each time.
- Known limitation carried forward: this gives a **repeatable default/reset layout**, not restoration of wherever an operator last dragged a window — GTK4/Wayland still has no API for the app to read/set toplevel x/y, so `layout.ini` still only persists size + visible/minimized/maximized state, never position. If an operator's dragged arrangement must survive SAVE/LOAD, that requires option 2 (in-app managed workspace) instead.

## Window size persistence fix (2026-07-09)

- Root cause of SAVE/LOAD not restoring size: `persistable_window_size()` was reading live widget/surface allocation instead of `gtk_window_get_default_size()`. GTK4's own docs for `gtk_window_set_default_size()` warn that using the widget allocation instead of `get_default_size()` "will not work in all circumstances and can lead to growing or shrinking windows" — exactly the symptom seen.
- Fix: `persistable_window_size()` now reads `gtk_window_get_default_size()` directly (GTK4 keeps this in sync with live resizes except while maximized/fullscreened), falling back to the window's factory default only if unset. `apply_window_size()` (unchanged) applies saved size via `gtk_window_set_default_size()`, which per the same docs behaves like a live resize even on an already-shown window, not just at first show.

## Kiosk configuration approach

- The existing kiosk config does not need to be preserved; we can replace it with a clean demo-oriented setup.
- Use an admin SSH account with sudo to provision the VM, copy the binary, install the launcher, and write GDM settings.
- Keep `kioskusr` runtime-only and launch the demo from `~/.local/bin/gnome-kiosk-script`.
- On update, copy the new build and reboot the VM rather than trying to restart the kiosk session in place.
- Provisioning should set GNOME power/session defaults for kiosk reliability (no blanking/sleep/lock) via dconf.
- Window locking for alert-style popups should be handled later with real toplevel window behavior, not in-app MDI widgets.

## Remote access topology options (same live kiosk desktop)

Primary implementation target for this demo is a **single RHEL-compatible path**:
- Server: GNOME Remote Desktop RDP backend on port 3389
- Client: `xfreerdp`
- Provisioning: boot-path only (no runtime SSH mutation of kiosk session state)

Other options below are retained as reference/experiments, not the default direction.

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
- TigerVNC compatibility note: ensure GNOME VNC encryption is set to `['none']` for password-auth interop; `['tls-anon']` can cause "No matching security types".
- Test alternative endpoint:
  - `vncviewer -Shared 192.168.122.79:5902`
- `remote-viewer` multi-session testing:
  - start separate processes per endpoint (e.g. one process to `:5901`, another to `:5902`)
  - if needed, force separate launches by running them from separate terminals
- RDP boot-path config now uses credentials auth with fixed port `3389`; test using `xfreerdp` or `remmina` against `192.168.122.79:3389`.
- `remote-viewer` may not include RDP support in some builds; for this demo baseline use `xfreerdp`.

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

**Confirmed broken for concurrent viewing (2026-07-09):** the user tested this directly — first VNC connection works, second just hangs waiting, never actually connects. GNOME's VNC backend (`gnome-remote-desktop`) does not appear to support true concurrent multi-viewer RFB sharing the way standalone VNC servers (TigerVNC, x11vnc) do; it services one active viewer and leaves a second connection half-open. The `socat`-based fanout (5901/5902 → 5900) can't fix this — it's a dumb byte-forwarder sitting in front of a backend that's the actual bottleneck. **This whole VNC-based role-split scheme does not deliver its intended purpose (two concurrent viewers) and should be treated as non-functional for that goal**, not just "for testing convenience" as originally framed above. RDP was already the designated primary path (see "Remote access topology options" below) — this finding reinforces staying on RDP, it doesn't require any rework of the RDP-based plan.

## Multi-operator input attribution (2026-07-09)

Requirement: with two RDP operators (`user`, `supervisor`) sharing the same live kiosk desktop, the app needs to know which one generated a given input event, and needs to be able to lock a window to supervisor-only.

**Empirically confirmed dead end, precise mechanism (refined 2026-07-09):** GTK/Wayland cannot distinguish input source across concurrent RDP sessions sharing one live desktop — but this is **not** a general "Wayland hides device identity from apps" policy. `gdk_event_get_device()`/`gdk_event_get_seat()` are real, normal APIs (used for the diagnostic test below) and genuinely do report device/seat identity when it exists. Two more specific facts combine to produce the dead end:
1. The core Wayland pointer/keyboard wire protocol (`wl_pointer`/`wl_keyboard`, scoped to `wl_seat`) simply has no per-device field in its messages — this mirrors X11's default "Core Pointer"/"Core Keyboard" behavior (that's literally where the name comes from), where two ordinary physical mice have *always* merged into one cursor unless an app specifically reaches for XInput2, which almost none do. Real per-device disambiguation was never the default on either display protocol.
2. Real distinguishability on Wayland requires separate **seats**, not separate devices — and Mutter's remote-desktop backend in `screen-share-mode=mirror-primary` deliberately injects every incoming RDP connection's input through the *one* seat the physical/kiosk session already owns, rather than creating a per-connection seat. That's intentional: mirror-primary's whole purpose is "make the remote operator act as if physically present at that one existing seat." If it created a fresh seat per connection instead, `gdk_event_get_seat()` would show distinct objects and this problem would not exist.

Tested by instrumenting `src/main.c` with a temporary diagnostic input-device logger attached to every window via a `GtkEventControllerLegacy` in `GTK_PHASE_CAPTURE`, deploying to the RHEL10 VM, and driving distinct input into two concurrent `xfreerdp` sessions via `xdotool`. Every event from both sessions logged the identical `GdkDevice`/`GdkSeat` pointer ("Core Pointer"/"Core Keyboard"). This holds regardless of how many RDP/VNC listener ports or daemon processes front that one mirrored session — confirmed architecturally (not just empirically), since it's specifically `mirror-primary`'s one-seat design, not a protocol-wide restriction. A GNOME Shell extension does **not** sidestep this either (unlike the window-position problem) — extensions read the same merged seat/device event pipeline, since the seat-sharing happens in Mutter's remote-desktop backend, below where a Shell extension would hook in.

**Follow-up bug this diagnostic caused, now fixed (2026-07-09):** the instrumentation broke minimize (and, by the same mechanism, any button click) across the whole app once deployed. Root cause: GTK4's own docs for `GtkEventControllerLegacy` state it "consumes all events" — attaching one to every toplevel swallowed button-press events before GTK's normal widget/gesture dispatch ever saw them, regardless of the handler returning `GDK_EVENT_PROPAGATE`. This is a real GTK4 gotcha worth remembering if raw/legacy event inspection is ever needed again: don't attach `GtkEventControllerLegacy` to widgets that also need normal click handling: use a non-consuming controller (e.g. `GtkGestureClick` in capture phase with `gtk_gesture_single_set_button(0)` to observe all buttons) instead. The diagnostic controller and its logging (`on_debug_input_event`, `input_debug_log`) have been fully removed from `src/main.c` — it already served its purpose for the investigation above.

## Window minimize — open gap on RHEL10, unresolved (2026-07-09)

Once the event-consuming `GtkEventControllerLegacy` bug above was fixed, minimize/restore worked correctly on Fedora44 but not on RHEL10, on byte-identical code. Confirmed by direct comparison: on RHEL10 the button label toggles correctly (`on_menu_button_clicked` runs fine) and the window gets focus/selection on restore, but the window itself never actually hides or gets raised. Root cause: RHEL10 ships `mutter 49.4`/`gnome-kiosk 49.0` (vs Fedora44's `mutter 50.1`/`gnome-kiosk 50.0`), and the older version does not honor `xdg_toplevel.set_minimized` — `gtk_window_minimize()`/`gtk_window_unminimize()` are accepted but silently no-op there. Genuine version-specific compositor gap, not a code regression.

Two app-level workarounds were tried and both failed:
1. Made `gtk_widget_set_visible()` the authoritative show/hide instead of relying on compositor minimize. Fixed RHEL10's hide but broke restore-position on **both** platforms: `set_visible(FALSE)` fully unmaps the Wayland surface, and a compositor only remembers a toplevel's position/stacking while it stays mapped — restoring via `set_visible(TRUE)` is functionally a new window, landing top-left with lost z-order. Same underlying Wayland position-tracking limitation documented above for SAVE/LOAD position, triggered a different way.
2. Kept the surface mapped and faked "minimized" via `gtk_widget_set_opacity(win->window, 0.0)` + `gtk_widget_set_can_target(win->window, FALSE)` (should preserve position/stacking since nothing unmaps). This did not render as invisible on RHEL10's `gtk4 4.16.7`/`mutter 49.4` stack — window stayed fully visible. Not root-caused; plausibly a toplevel-opacity compositing gap on that older GTK4/mutter combination, but unconfirmed.

**Current state: reverted to the plain, original `gtk_window_minimize()`-based code (matches the last committed baseline exactly, see the comment left in `set_window_minimized()`).** This is a known, named, unresolved gap — not silently worked around. Decision explicitly deferred by the user (2026-07-09): don't layer further workarounds on without direction. Two things the user is separately pursuing:
- Considering filing a RHEL Bugzilla against `mutter`/`gnome-kiosk` not honoring `xdg_toplevel.set_minimized`, to get visibility into whether/when a fix might land in a RHEL10 point release — **not** interested in manually upgrading mutter on RHEL10 (unsupported ABI risk, and defeats the demo's point of proving this works on stock RHEL10). If asked to help prepare that report, gather exact versions + minimal repro rather than assuming one exists yet.
- Re-evaluating the isolated-per-role-sessions idea (see `docs/rdp-input-attribution.md` / [[input-attribution-investigation]]) in light of this: if a single app instance can't reliably control its own window's minimize/position through GTK4 on this stack, an app instance driving another session's window state via D-Bus sync faces the same unreliable primitives plus cross-process coordination on top. This is a real, not-yet-resolved concern against that design, raised by the user directly — don't treat isolated-sessions as settled.

**Takeaway for future window-state work on this project:** don't assume `gtk_window_minimize()`/`maximize()`/similar WM-hint APIs are honored by `gnome-kiosk` — verify on both VMs, since RHEL10 and Fedora44 run meaningfully different `mutter` versions. And note for the record: unmapping a GTK widget does **not** destroy it or its data — a hidden window's `GtkTextBuffer` keeps updating normally on the data tick — the only thing lost on unmap is the compositor's memory of screen position/stacking.

**Two real fixes identified**, full writeup and comparison in [docs/rdp-input-attribution.md](docs/rdp-input-attribution.md):
1. **`freerdp-proxy` + custom plugin** — packaged and confirmed installable on both Fedora44 and RHEL10 (RHEL10: via the CodeReady Builder repo). A real, maintained MITM RDP proxy with a C plugin API; terminate role connections upstream of gnome-remote-desktop with distinct credentials, gate/attribute input in a custom plugin before it reaches the shared seat. Real enforced boundary, keeps one literal shared framebuffer. Cost: plugin dev against FreeRDP's upstream proxy-module API (no local devel headers for it, only the runtime binary), IPC to the app, region negotiation.
2. ~~Isolated per-role sessions via GNOME/GDM headless remote login~~ — **considered dead as of 2026-07-09 (later the same day), not just open.** Originally: confirmed present on both machines (`gnome-remote-desktop.service --system` + `gnome-headless-session@.service` + `gdm-headless-login-session`), give `user`/`supervisor` each their own Linux account/session so there's no shared seat to lose identity on. Closed for two reasons the user reasoned through directly: (a) syncing window state between two independent sessions requires each session to first reliably control its *own* window position/state, which the minimize investigation above showed doesn't reliably work even for one session on this stack; (b) more fundamentally, the actual requirement (told to the user for non-technical reasons) is that both operators see the **literally identical** window arrangement in real time, not just the same data arranged independently — two independent sessions, even perfectly synced, are definitionally two different windows, not one. A single shared desktop isn't just easier, it's the only architecture that satisfies the actual requirement.

**Status as of 2026-07-09: `freerdp-proxy` + custom plugin (option 1 above) is the only remaining live path for a genuine enforced lock.** Decision on whether to build it is still pending — user was taking the memo to the organization before committing. Don't start building without that direction.