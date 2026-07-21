import { Extension } from 'resource:///org/gnome/shell/extensions/extension.js';
import Gio from 'gi://Gio';
import Meta from 'gi://Meta';

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
  </interface>
</node>`;

const PROTOCOL_VERSION = 'kiosk-layout-helper/1';

// Must match the DemoWindow.title strings in src/main.c (WINDOW_SPECS).
const KNOWN_TITLES = [
    'EXTERNAL INTERFACES',
    'TRAFFIC OVERVIEW',
    'CLEARANCE QUEUE',
    'GROUND OPS',
    'SYSTEM STATUS',
];

function findWindows() {
    return global.get_window_actors()
        .map(actor => actor.meta_window)
        .filter(win => win && KNOWN_TITLES.includes(win.get_title()));
}

export default class KioskLayoutHelperExtension extends Extension {
    enable() {
        this._dbusImpl = Gio.DBusExportedObject.wrapJSObject(IFACE_XML, this);
        this._dbusImpl.export(Gio.DBus.session, '/org/gnomekiosk/LayoutHelper');
    }

    disable() {
        this._dbusImpl?.unexport();
        this._dbusImpl = null;
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
                win.unmaximize(Meta.MaximizeFlags.HORIZONTAL | Meta.MaximizeFlags.VERTICAL);

            if (minimized)
                win.minimize();
            else
                win.unminimize();
        }
    }
}
