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

gtk_dep = dependency('gtk4')

executable(
  'gnomekiosk-demo',
  'src/main.c',
  dependencies: [gtk_dep],
  install: false,
)
