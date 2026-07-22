project(
  'gnomekiosk-demo',
  'c',
  version: '0.1.0',
  meson_version: '>=1.2.0',
  default_options: [
    'c_std=c11',
    'warning_level=2',
  ],
)

cc = meson.get_compiler('c')

gtk_dep = dependency('gtk4')
gio_dep = dependency('gio-2.0')
m_dep = cc.find_library('m')

common_src = ['src/demo-common.c']

# Plain GTK4 build for gnome-kiosk: no D-Bus/layout-helper code at all.
executable(
  'gnomekiosk-demo',
  ['src/gnome-kiosk.c'] + common_src,
  dependencies: [gtk_dep, m_dep],
  install: false,
)

# gnome-shell build: adds exact position + reliable minimize via the
# "Kiosk Layout Helper" extension (gnome-shell-extension/).
executable(
  'gnomekiosk-demo-shell',
  ['src/gnome-shell.c'] + common_src,
  dependencies: [gtk_dep, gio_dep, m_dep],
  install: false,
)
