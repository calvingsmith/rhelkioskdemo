import { Extension } from 'resource:///org/gnome/shell/extensions/extension.js';
import Gio from 'gi://Gio';
import Meta from 'gi://Meta';
import Clutter from 'gi://Clutter';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';

const IFACE_XML = `
<node>
  <interface name="org.gnomekiosk.LayoutHelper">
    <method name="Ping">
      <arg type="s" direction="out" name="version"/>
    </method>
    <method name="GetStates">
      <arg type="a(siiiibb)" direction="out" name="windows"/>
    </method>
    <method name="SetStates">
      <arg type="a(siiiibb)" direction="in" name="windows"/>
    </method>
    <method name="ConfigureKioskChrome">
      <arg type="i" direction="in" name="barHeight"/>
    </method>
    <method name="SetWindowMinimized">
      <arg type="s" direction="in" name="title"/>
      <arg type="b" direction="in" name="minimized"/>
    </method>
  </interface>
</node>`;

const PROTOCOL_VERSION = 'kiosk-layout-helper/1';

// Must match the DemoWindow.title strings in src/demo-common.c (WINDOW_SPECS).
const KNOWN_TITLES = [
    'EXTERNAL INTERFACES',
    'TRAFFIC OVERVIEW',
    'CLEARANCE QUEUE',
    'GROUND OPS',
    'SYSTEM STATUS',
];

// Must match MENU_BAR_TITLE / the radar window's title in src/demo-common.h.
const MENU_BAR_TITLE = 'M & C GLOBAL MENU';
const RADAR_TITLE = 'EXTERNAL INTERFACES';

function findWindows() {
    return global.get_window_actors()
        .map(actor => actor.meta_window)
        .filter(win => win && KNOWN_TITLES.includes(win.get_title()));
}

function findWindowByTitle(title) {
    return global.get_window_actors()
        .map(actor => actor.meta_window)
        .find(win => win && win.get_title() === title);
}

export default class KioskLayoutHelperExtension extends Extension {
    enable() {
        this._dbusImpl = Gio.DBusExportedObject.wrapJSObject(IFACE_XML, this);
        this._dbusImpl.export(Gio.DBus.session, '/org/gnomekiosk/LayoutHelper');
        Main.panel.hide();

        // GNOME Shell shows the Activities Overview by default on a fresh
        // login; an operator should never see it in a locked-down kiosk.
        // A one-time hide() here can race Shell's own first-login logic
        // (the same kind of ordering race ConfigureKioskChrome hit), so
        // also guard against it being shown again for any reason later.
        Main.overview.hide();
        this._overviewShowingId = Main.overview.connect('showing', () => Main.overview.hide());

        // Root-caused 2026-07-22: our kiosk boots via systemd --user units
        // instead of a normal interactive login, so GNOME Shell's own
        // startup-animation "curtain" (a full-monitor-sized Clutter.Actor
        // it fades out while the session starts) never gets torn down --
        // its teardown is gated on a "session is ready" signal our
        // non-standard startup path apparently never delivers (the same
        // root issue as the GDM-readiness-timeout race worked around
        // elsewhere in this project, just surfacing inside Shell's JS
        // instead of GDM/logind this time). The leftover curtain stays
        // reactive=true, opacity=0, sized to exactly the monitor, and
        // silently absorbs every click before it can reach any real window
        // -- confirmed via captured-event diagnostics below (every click
        // picked this exact actor, regardless of on-screen position).
        // Rather than call Shell's private cleanup method (version-fragile
        // internals), neutralize any actor matching this observable
        // signature directly, watching for it since it's added to uiGroup
        // sometime after enable() runs, not before.
        const neutralizeIfStuckCurtain = (actor) => {
            const monitor = Main.layoutManager.primaryMonitor;
            if (!monitor || !actor.reactive || actor.opacity !== 0)
                return;
            if (actor.width !== monitor.width || actor.height !== monitor.height)
                return;
            console.log(`[kiosk-layout-helper] Neutralizing leftover full-screen invisible reactive actor (stuck Shell startup curtain): ${actor.toString()}`);
            actor.reactive = false;
        };
        // Wrapped in try/catch: a bug here previously threw uncaught (wrong
        // signal name) and silently aborted the rest of enable(), including
        // the unrelated click-diagnostics below. Never let this block take
        // down anything after it again.
        try {
            Main.uiGroup.get_children().forEach(neutralizeIfStuckCurtain);
            this._actorAddedId = Main.uiGroup.connect('child-added', (group, actor) => neutralizeIfStuckCurtain(actor));
        } catch (e) {
            console.log(`[kiosk-layout-helper] ERROR setting up curtain neutralizer: ${e}`);
        }

        // TEMPORARY diagnostic for the click-unresponsiveness investigation
        // (2026-07-22): confirms whether Mutter's Clutter stage sees button
        // events at all. Returns EVENT_PROPAGATE so nothing is consumed,
        // unlike the earlier GtkEventControllerLegacy mistake on the app
        // side. Remove once the freeze is root-caused.
        this._debugEventId = global.stage.connect('captured-event', (actor, event) => {
            const type = event.type();
            if (type === Clutter.EventType.BUTTON_PRESS || type === Clutter.EventType.BUTTON_RELEASE) {
                const src = event.get_source();
                const [x, y] = event.get_coords();
                // event.get_source() may not be resolved yet during the
                // capture phase, so also do our own pick directly so the
                // result doesn't depend on Clutter's event-lifecycle timing.
                const picked = global.stage.get_actor_at_pos(Clutter.PickMode.ALL, x, y);
                const grabActor = global.stage.get_grab_actor ? global.stage.get_grab_actor() : 'n/a';

                // TEMPORARY (2026-07-22): dump uiGroup's children at the
                // moment of the FIRST real click, so the listing is a true
                // contemporaneous snapshot instead of one taken at enable()
                // time (which was ~6s too early and missed whatever this is).
                if (!this._dumpedUiGroup) {
                    this._dumpedUiGroup = true;
                    const children = Main.uiGroup.get_children();
                    console.log(`[kiosk-layout-helper] DEBUG (at click) uiGroup has ${children.length} children:`);
                    children.forEach((c, i) => {
                        console.log(`[kiosk-layout-helper] DEBUG   [${i}] ${c.constructor.name}(${c.get_name?.() ?? 'noname'}) reactive=${c.reactive} visible=${c.visible} opacity=${c.opacity} size=${c.width}x${c.height} isPicked=${c === picked} ptr=${c.toString()}`);
                    });
                }

                // Walk the ancestor chain up to the stage, naming each actor,
                // and check identity against the well-known scene-graph
                // groups, so we can pin down exactly what's absorbing clicks
                // without needing another guess-and-reboot round.
                let chain = [];
                let a = picked;
                for (let i = 0; a && i < 15; i++) {
                    chain.push(`${a.constructor.name}(${a.get_name?.() ?? 'noname'})[reactive=${a.reactive}]`);
                    a = a.get_parent ? a.get_parent() : null;
                }
                const identity =
                    picked === Main.uiGroup ? 'Main.uiGroup' :
                    picked === global.window_group ? 'global.window_group' :
                    picked === global.top_window_group ? 'global.top_window_group' :
                    picked === Main.layoutManager?._backgroundGroup ? 'layoutManager._backgroundGroup' :
                    'UNKNOWN';

                console.log(`[kiosk-layout-helper] DEBUG captured-event type=${type} button=${event.get_button()} at=${x},${y} source=${src ? src.toString() : 'none'} picked=${picked ? picked.toString() : 'NONE'} identity=${identity} chain=${chain.join(' -> ')} actionMode=${Main.actionMode} modalCount=${Main.modalCount} overviewVisible=${Main.overview.visible} grabActor=${grabActor ? grabActor.toString() : 'NONE'}`);
            }
            return Clutter.EVENT_PROPAGATE;
        });
    }

    disable() {
        this._dbusImpl?.unexport();
        this._dbusImpl = null;
        Main.panel.show();
        if (this._overviewShowingId) {
            Main.overview.disconnect(this._overviewShowingId);
            this._overviewShowingId = null;
        }
        if (this._debugEventId) {
            global.stage.disconnect(this._debugEventId);
            this._debugEventId = null;
        }
        if (this._actorAddedId) {
            Main.uiGroup.disconnect(this._actorAddedId);
            this._actorAddedId = null;
        }
    }

    // Called once at app startup to confirm the extension is installed,
    // enabled, and reachable before relying on GetStates/SetStates.
    Ping() {
        return PROTOCOL_VERSION;
    }

    // Called by the kiosk app's SAVE button.
    GetStates() {
        return findWindows().map(win => {
            const rect = win.get_frame_rect();
            return [
                win.get_title(),
                rect.x, rect.y, rect.width, rect.height,
                win.minimized,
                win.get_maximize_flags() !== 0,
            ];
        });
    }

    // Called by the kiosk app's LOAD/RESET button.
    SetStates(windows) {
        const found = findWindows();
        for (const [title, x, y, width, height, minimized, maximized] of windows) {
            const win = found.find(w => w.get_title() === title);
            if (!win)
                continue;

            win.move_resize_frame(true, x, y, width, height);

            if (maximized)
                win.maximize(Meta.MaximizeFlags.HORIZONTAL | Meta.MaximizeFlags.VERTICAL);
            else
                win.unmaximize(); // unmaximize() takes no arguments on this Mutter version

            if (minimized)
                win.minimize();
            else
                win.unminimize();
        }
    }

    // Called once at startup (and again after RESET/LOAD, which can
    // re-maximize the radar window) to pin the standalone menu bar as an
    // unmovable always-on-top DOCK, and resize the radar window to fill
    // the rest of the monitor below it so the two don't overlap.
    ConfigureKioskChrome(barHeight) {
        const monitor = Main.layoutManager.primaryMonitor;
        if (!monitor) {
            console.log('[kiosk-layout-helper] ConfigureKioskChrome: no primary monitor, aborting');
            return;
        }

        const bar = findWindowByTitle(MENU_BAR_TITLE);
        console.log(`[kiosk-layout-helper] ConfigureKioskChrome: bar ${bar ? 'found' : 'NOT FOUND'}, monitor ${monitor.width}x${monitor.height}+${monitor.x}+${monitor.y}`);
        if (bar) {
            bar.set_type(Meta.WindowType.DOCK);
            bar.make_above();
            bar.stick();
            bar.move_resize_frame(true, monitor.x, monitor.y, monitor.width, barHeight);
        }

        const radar = findWindowByTitle(RADAR_TITLE);
        console.log(`[kiosk-layout-helper] ConfigureKioskChrome: radar ${radar ? 'found' : 'NOT FOUND'}`);
        if (radar) {
            if (radar.is_maximized())
                radar.unmaximize(); // unmaximize() takes no arguments on this Mutter version
            radar.move_resize_frame(true, monitor.x, monitor.y + barHeight,
                monitor.width, monitor.height - barHeight);
        }
    }
}
