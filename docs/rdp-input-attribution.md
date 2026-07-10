# Who clicked that?

Determining which remote operator is driving a window on a single shared kiosk desktop — and whether one window can be locked to a supervisor only.

**Project:** gnome-kiosk flight-control demo
**Date:** 2026-07-09
**Prepared for:** internal review
**Status:** Investigation complete — direction needed

## In short

Two people can already share one live kiosk desktop over RDP. We tested whether the desktop itself can tell them apart — it can't. GTK, GNOME and the Wayland protocol all merge remote input into a single pointer and keyboard before any application sees it, and no combination of ports, credentials, or listener processes changes that while the desktop stays literally shared.

Two real fixes exist. One rewrites the network path (a custom RDP proxy). The other gives every operator their own session from the start, using GNOME's existing headless-login machinery, and keeps the two sessions visually in sync. We recommend the second — see §5.

## §1 — The requirement

Restated in plain terms, without the implementation detail below.

The kiosk demo is a flight-control-style desktop viewed by two remote roles at once — an operator (`user`) and a supervisor — connected over RDP to the same running session, the same way two people might look at one physical monitor. The application needs two things it doesn't have today:

1. A reliable way to know which of the two connected people just moved a window, clicked a button, or typed into a field.
2. The ability to designate one window as supervisor-only, so the operator role can see it but not act on it.

## §2 — What we ruled out

In the order we tested it. Each step narrowed the problem down to its real architectural cause rather than a fixable bug.

### 01 · Baseline — Shared desktop, mirrored to every client *(as designed)*

The kiosk runs one GNOME session; GNOME Remote Desktop's RDP backend mirrors that session's screen to however many clients connect (`screen-share-mode=mirror-primary`). This is intentional and is what makes the "two people watching the same live board" premise work at all.

### 02 · Tested — Can the app tell two RDP connections apart? *(No — confirmed)*

We instrumented the application to log the input device behind every click and keystroke, deployed it to the RHEL10 test VM, opened two concurrent RDP connections, and drove distinct input into each (a click and a keypress per session). Every event, from either session, was attributed to the exact same device and the exact same seat:

```
window=radar device=0xa292e90 name="Core Keyboard" seat=0xa2710b0   ← session A, key
window=radar device=0xa292c50 name="Core Pointer"  seat=0xa2710b0   ← session A, click
window=radar device=0xa292e90 name="Core Keyboard" seat=0xa2710b0   ← session B, key
window=radar device=0xa292c50 name="Core Pointer"  seat=0xa2710b0   ← session B, click
```

Wayland's seat model exists specifically to present "the" keyboard and "the" pointer to applications, deliberately hiding which physical or virtual device produced an event. Mutter injects RDP input the same way it would a real mouse plugged into the machine — by design, indistinguishably.

### 03 · Considered — Separate RDP ports or listener processes *(Doesn't change the outcome)*

Our first instinct was that step 02 only proved two connections to *one* listener collapse — maybe two independent listener processes, each with its own port and credentials, would stay distinguishable further upstream. They don't: `mirror-primary` mode is defined as reusing the kiosk session's one existing seat, regardless of how many processes or ports are in front of it. There is only one seat on this machine to inject into.

### 04 · Considered — A GNOME Shell extension *(Same wall, different door)*

Full GNOME Shell (as opposed to the minimal `gnome-kiosk` compositor we use) supports JavaScript extensions with direct access to Mutter's internals — genuinely useful for other problems, such as pixel-exact window placement. It does not help here: extensions read events off the same merged seat pipeline that step 02 just showed collapses both sessions into one device.

## §3 — What actually works

Both real fixes share one insight: attribution has to happen *before* input reaches the shared seat, because nothing downstream of it can recover the information.

```
Today (merged)                       Path B (isolated)
─────────────────                    ─────────────────
[operator RDP] [supervisor RDP]      [operator RDP] [supervisor RDP]
       \            /                       |               |
        \          /                    [own seat]      [own seat]
     [one shared seat]
      identity lost
```

### Path A — RDP proxy with a custom plugin

`freerdp-proxy` (packaged and confirmed installable on both the Fedora44 and RHEL10 machines) is a maintained, protocol-aware man-in-the-middle proxy with a C plugin API. It sits in front of the existing shared desktop, terminates each role's connection with its own credentials, and hands a plugin real per-connection identity before forwarding to Mutter.

| | |
|---|---|
| **Enforcement** | Hard — can drop input aimed at a locked region before it's ever merged |
| **Shared view** | Stays literally one framebuffer, one session |
| **New work** | Custom plugin against FreeRDP's upstream proxy-module API (not locally documented), IPC to the app, region negotiation |
| **Main risk** | Protocol-level code we'd own long-term; API surface not yet explored beyond the binary's existence |

### Path B — Isolated per-role sessions *(Recommended)*

Give each role its own Wayland seat from the start, using GNOME's own headless remote-login support — confirmed present on both machines (`gnome-remote-desktop.service --system`, `gnome-headless-session@.service`, `gdm-headless-login-session`). Each RDP login maps to its own Linux account and gets a fresh, fully isolated kiosk session. A small sync layer keeps both sessions' displays showing the same data and layout.

| | |
|---|---|
| **Enforcement** | Hard — each session's own app instance controls its own interactivity; nothing to intercept |
| **Shared view** | Kept in sync by us, not literally one framebuffer |
| **New work** | Two-account provisioning, a data/layout sync channel between app instances |
| **Main risk** | Sync layer must stay convincingly tight, or the "shared board" illusion cracks |

## §4 — Side by side

| | Path A — RDP proxy plugin | Path B — isolated sessions |
|---|---|---|
| Uses supported GNOME/GDM features | **No** — custom protocol code | **Yes** — existing headless-login path |
| Desktop stays literally shared | **Yes**, one framebuffer | **No**, kept in sync instead |
| Lock enforcement | Hard, at the network edge | Hard, in-process per session |
| Where the new risk lives | A protocol proxy we maintain forever | A sync channel we maintain forever |
| Unknowns going in | Plugin API undocumented locally | Whether `gnome-kiosk-script` runs cleanly under headless login |

## §5 — Recommendation

Path B carries less long-term risk because it's built almost entirely from GNOME/GDM functionality that already ships and is already installed on both test machines — we'd be configuring existing subsystems, not authoring and maintaining protocol-level code against an API we haven't yet seen. The cost is conceptual: the demo stops being one literal shared screen and becomes two screens we keep identical. For a kiosk demo, that distinction is very unlikely to be visible to an operator — but it is a real architecture change worth naming explicitly rather than sliding past.

> **Decision needed:** Approve moving forward on Path B (isolated per-role sessions with a sync layer), so we can validate `gnome-kiosk-script` under GDM's headless login path and scope the sync channel — or direct us to Path A if a literal single shared framebuffer is a hard requirement.

## §6 — If Path B is approved, next to verify

- Confirm the `gnome-kiosk-script-wayland` session launches correctly when started by `gdm-headless-login-session` for a non-autologin account, not just at physical seat0.
- Design the sync channel: a small shared data source (so both sessions render identical synthetic flight data, not two independently-random feeds) plus window-layout events, likely over a local socket rather than the session D-Bus bus, since headless sessions don't share one.
- Decide how tightly the two sessions must track each other visually — structural state (window positions, visibility, content) is straightforward; frame-perfect cursor/animation parity is not, and is unlikely to be worth building.
- Provision the two role accounts and confirm RDP system-mode authentication maps each login to the correct isolated session.

---
*gnome-kiosk flight-control demo — internal feasibility memo — tested on Fedora44 & RHEL10 lab VMs*
